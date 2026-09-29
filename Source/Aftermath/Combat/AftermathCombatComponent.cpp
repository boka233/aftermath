#include "Combat/AftermathCombatComponent.h"

#include "Aftermath.h"
#include "Combat/AftermathBallistics.h"
#include "Combat/AftermathHitHistory.h"
#include "Combat/AftermathNoise.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Infected/AftermathInfected.h"
#include "Items/AftermathInventoryComponent.h"
#include "Items/AftermathItemActions.h"
#include "Items/AftermathItemDatabase.h"
#include "Net/UnrealNetwork.h"
#include "Player/AftermathCharacter.h"
#include "Survival/AftermathSurvivalComponent.h"
#include "TimerManager.h"

namespace
{
	/** Bullet flight step for server tracing (seconds). */
	constexpr float BulletStep = 0.02f;

	/** Blocking world geometry for bullets (players are tested separately against rewound capsules). */
	FCollisionObjectQueryParams BulletWorldObjects()
	{
		FCollisionObjectQueryParams P;
		P.AddObjectTypesToQuery(ECC_WorldStatic);
		P.AddObjectTypesToQuery(ECC_WorldDynamic);
		return P;
	}

	const TCHAR* ResultName(EAftermathShotResult R)
	{
		switch (R)
		{
		case EAftermathShotResult::Fired:     return TEXT("fired");
		case EAftermathShotResult::NoWeapon:  return TEXT("no_weapon");
		case EAftermathShotResult::NoAmmo:    return TEXT("no_ammo");
		case EAftermathShotResult::TooFast:   return TEXT("too_fast");
		case EAftermathShotResult::BadOrigin: return TEXT("bad_origin");
		case EAftermathShotResult::BadAim:    return TEXT("bad_aim");
		case EAftermathShotResult::Reloading: return TEXT("reloading");
		case EAftermathShotResult::Dead:      return TEXT("dead");
		default:                              return TEXT("?");
		}
	}

	const TCHAR* ZoneName(EAftermathHitZone Z)
	{
		switch (Z)
		{
		case EAftermathHitZone::Head:  return TEXT("head");
		case EAftermathHitZone::Torso: return TEXT("torso");
		case EAftermathHitZone::Arms:  return TEXT("arms");
		case EAftermathHitZone::Legs:  return TEXT("legs");
		default:                       return TEXT("none");
		}
	}
}

UAftermathCombatComponent::UAftermathCombatComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);
}

void UAftermathCombatComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UAftermathCombatComponent, bReloading);
}

void UAftermathCombatComponent::BeginPlay()
{
	Super::BeginPlay();
}

AAftermathCharacter* UAftermathCombatComponent::GetSurvivor() const
{
	return Cast<AAftermathCharacter>(GetOwner());
}

const FAftermathItemDef* UAftermathCombatComponent::GetHeldDef() const
{
	const AAftermathCharacter* C = GetSurvivor();
	const UAftermathInventoryComponent* Inv = C ? C->GetInventory() : nullptr;
	const FAftermathEquipSlot* Hands = Inv ? Inv->GetSlot(EAftermathWearSlot::Hands) : nullptr;
	return (Hands && !Hands->IsEmpty()) ? DefOf(Hands->Item.Def) : nullptr;
}

int32 UAftermathCombatComponent::GetHeldRounds() const
{
	const AAftermathCharacter* C = GetSurvivor();
	const UAftermathInventoryComponent* Inv = C ? C->GetInventory() : nullptr;
	const FAftermathEquipSlot* Hands = Inv ? Inv->GetSlot(EAftermathWearSlot::Hands) : nullptr;
	const FAftermathItemDef* Def = GetHeldDef();
	if (!Def || !Def->IsFirearm())
	{
		return -1;
	}
	return Hands->Item.HasMagazine() ? Hands->Item.AttachedMagRounds : 0;
}

// ---------------------------------------------------------------------------
// Client
// ---------------------------------------------------------------------------

void UAftermathCombatComponent::SetTriggerHeld(bool bHeld)
{
	bTriggerHeld = bHeld;
	if (!bHeld)
	{
		bFiredThisPress = false;
	}
}

void UAftermathCombatComponent::SetAiming(bool bAim)
{
	bAiming = bAim;
}

void UAftermathCombatComponent::RequestReload()
{
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		ServerHandleReload();
	}
	else
	{
		ServerReload();
	}
}

void UAftermathCombatComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	HitMarkerAge += DeltaTime;
	DamageIndicatorAge += DeltaTime;
	for (FTracer& T : Tracers) { T.Age += DeltaTime; }
	Tracers.RemoveAll([](const FTracer& T) { return T.Age > 0.25f; });

	const AAftermathCharacter* C = GetSurvivor();
	if (!C || !C->IsLocallyControlled() || C->IsDead() || !bTriggerHeld)
	{
		return;
	}

	const FAftermathItemDef* Def = GetHeldDef();
	if (Def && Def->IsFirearm())
	{
		// Semi-auto: one shot per press. Full auto: keep firing while held.
		if (!bFiredThisPress || Def->Firearm.bFullAuto)
		{
			ClientTryFire();
		}
	}
	else if (!bFiredThisPress)
	{
		ClientTryMelee();
	}
}

void UAftermathCombatComponent::ClientTryFire()
{
	AAftermathCharacter* C = GetSurvivor();
	APlayerController* PC = C ? Cast<APlayerController>(C->GetController()) : nullptr;
	const FAftermathItemDef* Def = GetHeldDef();
	UWorld* World = GetWorld();
	if (!PC || !Def || !World || bReloading)
	{
		return;
	}
	const double Now = World->GetTimeSeconds();
	if (Now < LocalNextShotTime)
	{
		return;
	}
	if (GetHeldRounds() <= 0)
	{
		bFiredThisPress = true; // dry fire: one click per press
		return;
	}
	LocalNextShotTime = Now + AftermathBallistics::ShotInterval(Def->Firearm.Rpm);
	bFiredThisPress = true;

	// Bullets leave from the camera line (what you aim at) - the server checks it's near the eye.
	FVector ViewLoc;
	FRotator ViewRot;
	PC->GetPlayerViewPoint(ViewLoc, ViewRot);
	const FVector Muzzle = ViewLoc + ViewRot.Vector() * 20.f;
	const int32 Seed = FMath::Rand();

	// Server time the shooter saw: our clock estimate of server time, minus nothing (the server
	// clamps rewind anyway). GameState's server time is replicated and smoothed.
	const AGameStateBase* GS = World->GetGameState();
	const double ShotTime = GS ? GS->GetServerWorldTimeSeconds() : Now;

	if (C->HasAuthority())
	{
		ServerHandleFire(Muzzle, ViewRot.Vector(), Seed, ShotTime);
	}
	else
	{
		ServerFire(Muzzle, ViewRot.Vector(), Seed, ShotTime);
	}

	// Local recoil kick (cosmetic; aim is re-read from the view on the next shot).
	const float Scale = bAiming ? 0.6f : 1.f;
	PC->AddPitchInput(-Def->Firearm.RecoilPitchDeg * Scale);
	PC->AddYawInput(FMath::FRandRange(-1.f, 1.f) * Def->Firearm.RecoilYawDeg * Scale);
}

void UAftermathCombatComponent::DebugSendFire(const FVector& Muzzle, const FVector& Direction)
{
#if !UE_BUILD_SHIPPING
	const UWorld* World = GetWorld();
	const double T = World ? World->GetTimeSeconds() : 0.0;
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		ServerHandleFire(Muzzle, Direction, 1, T);
	}
	else
	{
		ServerFire(Muzzle, Direction, 1, T);
	}
#endif
}

void UAftermathCombatComponent::ClientTryMelee()
{
	AAftermathCharacter* C = GetSurvivor();
	APlayerController* PC = C ? Cast<APlayerController>(C->GetController()) : nullptr;
	if (!PC)
	{
		return;
	}
	bFiredThisPress = true;
	FVector ViewLoc;
	FRotator ViewRot;
	PC->GetPlayerViewPoint(ViewLoc, ViewRot);
	if (C->HasAuthority())
	{
		ServerHandleMelee(ViewRot.Vector());
	}
	else
	{
		ServerMelee(ViewRot.Vector());
	}
}

void UAftermathCombatComponent::ClientHitConfirmed_Implementation(const FAftermathHitNotify& Hit)
{
	HitMarkerAge = 0.f;
	bLastHitKill = Hit.bKilled;
}

void UAftermathCombatComponent::ClientTookHit_Implementation(const FAftermathHitNotify& Hit)
{
	LastDamageFrom = Hit.FromLocation;
	DamageIndicatorAge = 0.f;
}

void UAftermathCombatComponent::MulticastShotFx_Implementation(const FAftermathShotFx& Fx)
{
	++TracersReceived;
	FTracer& T = Tracers.AddDefaulted_GetRef();
	T.From = Fx.Muzzle;
	T.To = Fx.Impact;
	T.bHit = Fx.bHitCharacter;
}

// ---------------------------------------------------------------------------
// Server
// ---------------------------------------------------------------------------

void UAftermathCombatComponent::ServerFire_Implementation(FVector_NetQuantize Muzzle, FVector_NetQuantizeNormal Direction, int32 Seed, double ShotTime)
{
	ServerHandleFire(Muzzle, Direction, Seed, ShotTime);
}

void UAftermathCombatComponent::ServerMelee_Implementation(FVector_NetQuantizeNormal Direction)
{
	ServerHandleMelee(Direction);
}

void UAftermathCombatComponent::ServerReload_Implementation()
{
	ServerHandleReload();
}

FAftermathShotOutcome UAftermathCombatComponent::ServerHandleFire(const FVector& Muzzle, const FVector& InDirection, int32 Seed, double ShotTime)
{
	FAftermathShotOutcome Out;
	AAftermathCharacter* Shooter = GetSurvivor();
	UWorld* World = GetWorld();
	UAftermathInventoryComponent* Inv = Shooter ? Shooter->GetInventory() : nullptr;
	auto Reject = [&](EAftermathShotResult R)
	{
		Out.Result = R;
		LastOutcome = Out;
		UE_LOG(LogAftermath, Log, TEXT("[Aftermath] ShotRejected reason=%s by=%s"), ResultName(R), *GetNameSafe(Shooter));
		return Out;
	};

	if (!Shooter || !World || !Inv || !Shooter->HasAuthority())
	{
		return Reject(EAftermathShotResult::NoWeapon);
	}
	if (Shooter->IsDead())
	{
		return Reject(EAftermathShotResult::Dead);
	}
	if (bReloading)
	{
		return Reject(EAftermathShotResult::Reloading);
	}
	FAftermathEquipSlot const* Hands = Inv->GetSlot(EAftermathWearSlot::Hands);
	const FAftermathItemDef* Def = (Hands && !Hands->IsEmpty()) ? DefOf(Hands->Item.Def) : nullptr;
	if (!Def || !Def->IsFirearm() || Hands->Item.Health <= 0.f)
	{
		return Reject(EAftermathShotResult::NoWeapon);
	}
	if (!Hands->Item.HasMagazine() || Hands->Item.AttachedMagRounds <= 0)
	{
		return Reject(EAftermathShotResult::NoAmmo);
	}

	// Rate of fire (small allowance for packet jitter bunching shots together).
	const double Now = World->GetTimeSeconds();
	const double Interval = AftermathBallistics::ShotInterval(Def->Firearm.Rpm);
	if (Now < ServerNextShotTime - Interval * 0.25)
	{
		return Reject(EAftermathShotResult::TooFast);
	}

	// Origin must be near where the shooter actually is.
	const FVector Eye = Shooter->GetInteractEyeLocation();
	if (FVector::Dist(Muzzle, Eye) > AftermathBallistics::MaxMuzzleOffset + Shooter->GetThirdPersonCameraReach())
	{
		return Reject(EAftermathShotResult::BadOrigin);
	}
	// Aim must roughly match the replicated view (a hacked client can't shoot backwards through itself).
	const FVector Direction = InDirection.GetSafeNormal();
	const FVector ViewDir = Shooter->GetBaseAimRotation().Vector();
	if (FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Direction, ViewDir), -1.f, 1.f))) > AftermathBallistics::MaxAimErrorDeg)
	{
		return Reject(EAftermathShotResult::BadAim);
	}

	// Valid shot: only now does it consume the fire-rate budget (a rejected request must not
	// delay the next legitimate shot).
	ServerNextShotTime = FMath::Max(Now, ServerNextShotTime) + Interval;

	// Spend the round (server state; replicates to the owner through the inventory).
	if (FAftermathItemInstance* Gun = Inv->FindItemMutable(Hands->Item.Uid))
	{
		--Gun->AttachedMagRounds;
		Gun->Health = FMath::Max(0.f, Gun->Health - 0.001f);
		Inv->MarkChanged();
	}

	// Spread: server-owned cone size, client seed for the offset (so both could draw the same tracer).
	const float Spread = bAiming || Shooter->GetStance() != EAftermathStance::Standing
		? Def->Firearm.SpreadAimDeg : Def->Firearm.SpreadHipDeg;
	const FVector ShotDir = AftermathBallistics::SpreadDirection(Direction, Spread, Seed);

	// Walk the bullet arc: world first (walls stop bullets), then rewound capsules along each segment.
	const UAftermathHitHistory* History = World->GetSubsystem<UAftermathHitHistory>();
	const double RewindTime = History ? History->ClampRewind(ShotTime) : Now;

	TArray<FVector> Path;
	AftermathBallistics::BuildPath(Muzzle, ShotDir, Def->Firearm.MuzzleVelocity, Def->Firearm.MaxRange, BulletStep, Path);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(AftermathBullet), true, Shooter);
	const FCollisionObjectQueryParams WorldObjects = BulletWorldObjects();

	// Candidate victims: everyone else alive (survivors and infected), with their pose at the rewind time.
	struct FCandidate { ACharacter* C; FAftermathPoseSample Pose; };
	TArray<FCandidate> Candidates;
	for (TActorIterator<ACharacter> It(World); It; ++It)
	{
		ACharacter* C = *It;
		if (C == Shooter || !AftermathCombat::IsHittable(C))
		{
			continue;
		}
		FAftermathPoseSample Pose;
		if (!History || !History->GetPoseAt(C, RewindTime, Pose))
		{
			Pose.Location = C->GetActorLocation();
			Pose.HalfHeight = C->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
			Pose.Radius = C->GetCapsuleComponent()->GetScaledCapsuleRadius();
			const AAftermathCharacter* S = Cast<AAftermathCharacter>(C);
			Pose.bProne = S && S->GetStance() == EAftermathStance::Prone;
		}
		Candidates.Add({ C, Pose });
	}

	float Travelled = 0.f;
	for (int32 i = 1; i < Path.Num() && !Out.bHitCharacter && !Out.bHitWorld; ++i)
	{
		const FVector A = Path[i - 1];
		FVector B = Path[i];

		FHitResult WorldHit;
		const bool bWall = World->LineTraceSingleByObjectType(WorldHit, A, B, WorldObjects, Params);
		if (bWall)
		{
			B = WorldHit.ImpactPoint;
		}

		// Nearest body along this (possibly shortened) segment.
		float BestT = 2.f;
		const FCandidate* Best = nullptr;
		FVector BestPoint = FVector::ZeroVector;
		for (const FCandidate& Cand : Candidates)
		{
			FVector P;
			float T;
			if (AftermathBallistics::SegmentHitsCapsule(A, B, Cand.Pose.Location, Cand.Pose.Radius, Cand.Pose.HalfHeight, P, T) && T < BestT)
			{
				BestT = T;
				Best = &Cand;
				BestPoint = P;
			}
		}

		if (Best)
		{
			Out.bHitCharacter = true;
			Out.Victim = Best->C;
			Out.ImpactPoint = BestPoint;
			const float Bottom = Best->Pose.Location.Z - Best->Pose.HalfHeight;
			const float Height01 = (BestPoint.Z - Bottom) / FMath::Max(1.f, Best->Pose.HalfHeight * 2.f);
			Out.Zone = AftermathBallistics::ZoneFromHeight(Height01, Best->Pose.bProne);
		}
		else if (bWall)
		{
			Out.bHitWorld = true;
			Out.ImpactPoint = WorldHit.ImpactPoint;
		}
		Travelled += FVector::Dist(A, Out.bHitCharacter || Out.bHitWorld ? Out.ImpactPoint : B);
	}
	Out.Distance = Travelled;
	Out.Result = EAftermathShotResult::Fired;
	if (!Out.bHitCharacter && !Out.bHitWorld)
	{
		Out.ImpactPoint = Path.Last();
	}

	if (Out.bHitCharacter)
	{
		// Damage falls off a little past half range.
		const float Falloff = FMath::GetMappedRangeValueClamped(FVector2D(Def->Firearm.MaxRange * 0.5f, Def->Firearm.MaxRange), FVector2D(1.f, 0.5f), Travelled);
		const float Base = Def->Firearm.Damage * AftermathBallistics::ZoneMultiplier(Out.Zone) * Falloff;
		const float Blood = Def->Firearm.BloodDamage * Falloff;
		Out.Damage = Base;
		ApplyHit(*Out.Victim.Get(), Out.Zone, Base, Blood, 1.f, Muzzle, Out.ImpactPoint);
	}

	if (UAftermathNoiseSubsystem* Noise = World->GetSubsystem<UAftermathNoiseSubsystem>())
	{
		Noise->Report(EAftermathNoiseKind::Gunshot, Muzzle, Def->Firearm.NoiseRadius, Shooter);
	}

	FAftermathShotFx Fx;
	Fx.Muzzle = Muzzle;
	Fx.Impact = Out.ImpactPoint;
	Fx.bHitCharacter = Out.bHitCharacter;
	MulticastShotFx(Fx);

	UE_LOG(LogAftermath, Display, TEXT("[Aftermath] Shot by=%s weapon=%s hit=%s zone=%s damage=%.0f dist=%.0f rounds_left=%d rewind_ms=%.0f"),
		*Shooter->GetName(), *Def->Id.ToString(),
		Out.bHitCharacter ? *Out.Victim->GetName() : (Out.bHitWorld ? TEXT("world") : TEXT("none")),
		ZoneName(Out.Zone), Out.Damage, Out.Distance, GetHeldRounds(), (Now - RewindTime) * 1000.0);

	LastOutcome = Out;
	return Out;
}

namespace AftermathCombat
{
	bool IsHittable(const AActor* Actor)
	{
		if (const AAftermathCharacter* S = Cast<AAftermathCharacter>(Actor))
		{
			return !S->IsDead();
		}
		if (const AAftermathInfected* I = Cast<AAftermathInfected>(Actor))
		{
			return !I->IsDead();
		}
		return false;
	}
}

void UAftermathCombatComponent::ApplyHit(ACharacter& Victim, EAftermathHitZone Zone, float Damage, float Blood, float CutChance,
	const FVector& From, const FVector& Impact)
{
	FAftermathHitNotify Notify;
	if (AAftermathCharacter* S = Cast<AAftermathCharacter>(&Victim))
	{
		Notify = ApplyHitToSurvivor(*S, Zone, Damage, Blood, CutChance, From, Impact, GetSurvivor());
	}
	else if (AAftermathInfected* I = Cast<AAftermathInfected>(&Victim))
	{
		Notify = I->ServerApplyHit(Zone, Damage, From, Impact, GetSurvivor());
	}
	else
	{
		return;
	}
	ClientHitConfirmed(Notify);
}

FAftermathHitNotify UAftermathCombatComponent::ApplyHitToSurvivor(AAftermathCharacter& Victim, EAftermathHitZone Zone, float Damage, float Blood,
	float CutChance, const FVector& From, const FVector& Impact, const AActor* Instigator)
{
	FAftermathHitNotify Notify;
	Notify.ImpactPoint = Impact;
	Notify.FromLocation = From;
	Notify.Zone = Zone;

	UAftermathSurvivalComponent* S = Victim.GetSurvival();
	UAftermathInventoryComponent* VictimInv = Victim.GetInventory();
	if (!S)
	{
		return Notify;
	}
	// Armor for the zone (helmet for head, plate carrier for torso).
	const FAftermathItemDef* ArmorDef = nullptr;
	FAftermathItemInstance* ArmorItem = nullptr;
	if (VictimInv && (Zone == EAftermathHitZone::Head || Zone == EAftermathHitZone::Torso))
	{
		const EAftermathWearSlot Slot = Zone == EAftermathHitZone::Head ? EAftermathWearSlot::Head : EAftermathWearSlot::Vest;
		const FAftermathEquipSlot* Worn = VictimInv->GetSlot(Slot);
		if (Worn && !Worn->IsEmpty())
		{
			ArmorItem = VictimInv->FindItemMutable(Worn->Item.Uid);
			const FAftermathItemDef* D = DefOf(Worn->Item.Def);
			ArmorDef = (D && D->IsArmor() && D->Armor.Zone == Zone) ? D : nullptr;
		}
	}
	const AftermathBallistics::FArmorResult R = AftermathBallistics::ApplyArmor(Damage, Blood,
		ArmorDef ? &ArmorDef->Armor : nullptr, ArmorItem ? ArmorItem->Health : 0.f);
	if (ArmorItem && R.ArmorWear > 0.f)
	{
		ArmorItem->Health = FMath::Max(0.f, ArmorItem->Health - R.ArmorWear);
		VictimInv->MarkChanged();
	}

	const bool bWasAlive = !S->IsDead();
	S->ApplyDamage(R.Damage, R.Blood);

	// Wounds: most bullet hits and sharp melee open a cut unless armor stopped most of it; leg hits can break bones.
	if (!S->IsDead())
	{
		const float Pen = Damage > 0.f ? R.Damage / Damage : 0.f;
		if (FMath::FRand() < CutChance * Pen)
		{
			S->AddCut(1);
		}
		if (Zone == EAftermathHitZone::Legs && R.Damage >= 25.f && FMath::FRand() < 0.35f)
		{
			S->BreakLegs();
		}
	}
	const bool bKilled = bWasAlive && S->IsDead();

	UE_LOG(LogAftermath, Display, TEXT("[Aftermath] Hit victim=%s zone=%s damage=%.0f blood=%.0f armor=%s killed=%d by=%s"),
		*Victim.GetName(), ZoneName(Zone), R.Damage, R.Blood, ArmorDef ? *ArmorDef->Id.ToString() : TEXT("none"), bKilled ? 1 : 0,
		*GetNameSafe(Instigator));

	Notify.Damage = R.Damage;
	Notify.bKilled = bKilled;
	if (UAftermathCombatComponent* VictimCombat = Victim.GetCombat())
	{
		VictimCombat->ClientTookHit(Notify);
	}

	if (bKilled)
	{
		UE_LOG(LogAftermath, Display, TEXT("[Aftermath] Kill killer=%s victim=%s zone=%s"),
			*GetNameSafe(Instigator), *Victim.GetName(), ZoneName(Zone));
	}
	return Notify;
	}

bool UAftermathCombatComponent::ServerHandleMelee(const FVector& InDirection)
{
	AAftermathCharacter* Attacker = GetSurvivor();
	UWorld* World = GetWorld();
	if (!Attacker || !World || !Attacker->HasAuthority() || Attacker->IsDead())
	{
		return false;
	}
	// Melee weapon in hands: use its stats. Anything else (empty hands, a gun, a tool): fists.
	const FAftermathItemDef* Def = GetHeldDef();
	const bool bArmed = Def && Def->IsMeleeWeapon();
	const float Damage = bArmed ? Def->Melee.Damage : FistDamage;
	const float Range = bArmed ? Def->Melee.Range : 150.f;
	const float Cooldown = bArmed ? Def->Melee.Cooldown : 0.6f;
	const float Stamina = bArmed ? Def->Melee.StaminaCost : 6.f;
	const float CutChance = bArmed ? Def->Melee.CutChance : 0.f;

	const double Now = World->GetTimeSeconds();
	if (Now < ServerNextMeleeTime - 0.05)
	{
		UE_LOG(LogAftermath, Log, TEXT("[Aftermath] MeleeRejected reason=too_fast by=%s"), *Attacker->GetName());
		return false;
	}
	ServerNextMeleeTime = Now + Cooldown;

	UAftermathSurvivalComponent* S = Attacker->GetSurvival();
	if (S)
	{
		S->SpendStamina(Stamina);
	}

	// Short sweep from the eye along the (server-checked) aim.
	const FVector Eye = Attacker->GetInteractEyeLocation();
	FVector Dir = InDirection.GetSafeNormal();
	const FVector ViewDir = Attacker->GetBaseAimRotation().Vector();
	if (FVector::DotProduct(Dir, ViewDir) < FMath::Cos(FMath::DegreesToRadians(AftermathBallistics::MaxAimErrorDeg)))
	{
		Dir = ViewDir;
	}
	const FVector End = Eye + Dir * Range;

	// Walls block swings too.
	FHitResult WallHit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(AftermathMelee), true, Attacker);
	const bool bWall = World->LineTraceSingleByObjectType(WallHit, Eye, End, BulletWorldObjects(), Params);
	const FVector Reach = bWall ? WallHit.ImpactPoint : End;

	ACharacter* Best = nullptr;
	float BestT = 2.f;
	FVector BestPoint = FVector::ZeroVector;
	for (TActorIterator<ACharacter> It(World); It; ++It)
	{
		ACharacter* C = *It;
		if (C == Attacker || !AftermathCombat::IsHittable(C))
		{
			continue;
		}
		const UCapsuleComponent* Cap = C->GetCapsuleComponent();
		FVector P;
		float T;
		// Melee is forgiving: +15cm radius.
		if (AftermathBallistics::SegmentHitsCapsule(Eye, Reach, C->GetActorLocation(), Cap->GetScaledCapsuleRadius() + 15.f,
			Cap->GetScaledCapsuleHalfHeight(), P, T) && T < BestT)
		{
			Best = C;
			BestT = T;
			BestPoint = P;
		}
	}

	if (UAftermathNoiseSubsystem* Noise = World->GetSubsystem<UAftermathNoiseSubsystem>())
	{
		Noise->Report(EAftermathNoiseKind::Melee, Eye, 800.f, Attacker);
	}

	if (!Best)
	{
		UE_LOG(LogAftermath, Display, TEXT("[Aftermath] Melee by=%s weapon=%s hit=none"), *Attacker->GetName(), bArmed ? *Def->Id.ToString() : TEXT("fists"));
		return false;
	}
	const float Bottom = Best->GetActorLocation().Z - Best->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const float Height01 = (BestPoint.Z - Bottom) / (Best->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() * 2.f);
	const AAftermathCharacter* BestSurvivor = Cast<AAftermathCharacter>(Best);
	const EAftermathHitZone Zone = AftermathBallistics::ZoneFromHeight(Height01, BestSurvivor && BestSurvivor->GetStance() == EAftermathStance::Prone);
	UE_LOG(LogAftermath, Display, TEXT("[Aftermath] Melee by=%s weapon=%s hit=%s zone=%s"),
		*Attacker->GetName(), bArmed ? *Def->Id.ToString() : TEXT("fists"), *Best->GetName(), ZoneName(Zone));
	// Melee zone multiplier is flatter than guns (a bat to the head isn't 3x).
	const float ZoneMul = Zone == EAftermathHitZone::Head ? 1.5f : (Zone == EAftermathHitZone::Torso ? 1.f : 0.8f);
	ApplyHit(*Best, Zone, Damage * ZoneMul, Damage * 4.f, CutChance, Eye, BestPoint);
	return true;
}

bool UAftermathCombatComponent::ServerHandleReload()
{
	AAftermathCharacter* C = GetSurvivor();
	UWorld* World = GetWorld();
	const FAftermathItemDef* Def = GetHeldDef();
	if (!C || !World || !C->HasAuthority() || C->IsDead() || bReloading || !Def || !Def->IsFirearm())
	{
		return false;
	}
	// Is there a better magazine at all? (Otherwise don't start the timer.)
	const UAftermathInventoryComponent* Inv = C->GetInventory();
	const int32 Current = GetHeldRounds();
	bool bHasBetter = false;
	for (const FAftermathEquipSlot& S : Inv->GetSlots())
	{
		for (const FAftermathGridEntry& E : S.Cargo.Entries)
		{
			if (Def->AcceptsMagazine(E.Item.Def) && E.Item.Loaded > Current)
			{
				bHasBetter = true;
			}
		}
	}
	if (!bHasBetter)
	{
		UE_LOG(LogAftermath, Display, TEXT("[Aftermath] Reload by=%s result=no_magazine"), *C->GetName());
		return false;
	}
	bReloading = true;
	C->ForceNetUpdate();
	World->GetTimerManager().SetTimer(ReloadTimer, this, &UAftermathCombatComponent::FinishReload, Def->Firearm.ReloadSeconds, false);
	return true;
}

void UAftermathCombatComponent::FinishReload()
{
	bReloading = false;
	AAftermathCharacter* C = GetSurvivor();
	if (!C || C->IsDead())
	{
		return;
	}
	const FAftermathEquipSlot* Hands = C->GetInventory()->GetSlot(EAftermathWearSlot::Hands);
	if (Hands && !Hands->IsEmpty())
	{
		const EAftermathActionResult R = AftermathItemActions::Reload(*C, Hands->Item.Uid);
		UE_LOG(LogAftermath, Display, TEXT("[Aftermath] Reload by=%s result=%s rounds=%d"),
			*C->GetName(), AftermathItemActions::ResultToString(R), GetHeldRounds());
	}
	C->ForceNetUpdate();
}
