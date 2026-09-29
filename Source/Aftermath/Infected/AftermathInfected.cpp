#include "Infected/AftermathInfected.h"

#include "Aftermath.h"
#include "Combat/AftermathNoise.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Infected/AftermathInfectedManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "NavigationData.h"
#include "NavigationInvokerComponent.h"
#include "NavigationSystem.h"
#include "NavMesh/RecastNavMesh.h"
#include "Net/UnrealNetwork.h"
#include "Player/AftermathCharacter.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	FCollisionObjectQueryParams SightBlockers()
	{
		FCollisionObjectQueryParams P;
		P.AddObjectTypesToQuery(ECC_WorldStatic);
		P.AddObjectTypesToQuery(ECC_WorldDynamic);
		return P;
	}

	const TCHAR* NoiseName(EAftermathNoiseKind Kind)
	{
		switch (Kind)
		{
		case EAftermathNoiseKind::Footstep: return TEXT("footstep");
		case EAftermathNoiseKind::Gunshot:  return TEXT("gunshot");
		case EAftermathNoiseKind::Melee:    return TEXT("melee");
		case EAftermathNoiseKind::Cough:    return TEXT("cough");
		case EAftermathNoiseKind::Door:     return TEXT("door");
		default:                            return TEXT("noise");
		}
	}

	const TCHAR* ZoneText(EAftermathHitZone Z)
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

	/** Swings mostly land on the upper body. */
	EAftermathHitZone RollSwingZone()
	{
		const float R = FMath::FRand();
		if (R < 0.15f) { return EAftermathHitZone::Head; }
		if (R < 0.65f) { return EAftermathHitZone::Torso; }
		if (R < 0.90f) { return EAftermathHitZone::Arms; }
		return EAftermathHitZone::Legs;
	}
}

AAftermathInfected::AAftermathInfected(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicateMovement(true);
	// Plenty for a walking/running body; far fewer updates than a player.
	SetNetUpdateFrequency(20.f);
	SetMinNetUpdateFrequency(4.f);

	// No AI controller: the server steers movement directly.
	AutoPossessAI = EAutoPossessAI::Disabled;
	AIControllerClass = nullptr;
	bUseControllerRotationYaw = false;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;

	GetCapsuleComponent()->InitCapsuleSize(35.f, 90.f);

	UCharacterMovementComponent* Move = GetCharacterMovement();
	Move->bRunPhysicsWithNoController = true;
	Move->bOrientRotationToMovement = false; // we turn the body ourselves (faces targets while swinging too)
	Move->MaxWalkSpeed = 150.f;
	Move->MaxAcceleration = 1200.f;
	Move->BrakingDecelerationWalking = 1400.f;
	// Infected don't climb or jump.
	Move->GetNavAgentPropertiesRef().bCanJump = false;
	Move->GetNavAgentPropertiesRef().bCanCrouch = false;

	PlaceholderBody = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PlaceholderBody"));
	PlaceholderBody->SetupAttachment(GetCapsuleComponent());
	PlaceholderBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PlaceholderBody->SetRelativeScale3D(FVector(0.6f, 0.6f, 1.8f));

	PlaceholderFace = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PlaceholderFace"));
	PlaceholderFace->SetupAttachment(GetCapsuleComponent());
	PlaceholderFace->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PlaceholderFace->SetRelativeLocation(FVector(28.f, 0.f, 65.f));
	PlaceholderFace->SetRelativeScale3D(FVector(0.12f, 0.3f, 0.12f));

	// Awake infected keep a navmesh patch around themselves (they can walk away from the players who woke them).
	NavInvoker = CreateDefaultSubobject<UNavigationInvokerComponent>(TEXT("NavInvoker"));
	NavInvoker->SetGenerationRadii(4000.f, 6000.f);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ShapeMat(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (Cylinder.Succeeded())
	{
		PlaceholderBody->SetStaticMesh(Cylinder.Object);
	}
	if (Cube.Succeeded())
	{
		PlaceholderFace->SetStaticMesh(Cube.Object);
	}
	if (ShapeMat.Succeeded())
	{
		PlaceholderBody->SetMaterial(0, ShapeMat.Object);
	}
}

void AAftermathInfected::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AAftermathInfected, TypeIndex);
	DOREPLIFETIME(AAftermathInfected, RepState);
	DOREPLIFETIME(AAftermathInfected, bDead);
}

void AAftermathInfected::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	TypeIndex = FMath::Min<uint8>(TypeIndex, static_cast<uint8>(EAftermathInfectedType::Soldier));
}

void AAftermathInfected::SetInfectedType(EAftermathInfectedType Type)
{
	TypeIndex = static_cast<uint8>(Type);
	Health = Def().Health;
	UpdateLook();
}

void AAftermathInfected::BeginPlay()
{
	Super::BeginPlay();

	if (GetNetMode() != NM_DedicatedServer && PlaceholderBody)
	{
		BodyMaterial = PlaceholderBody->CreateDynamicMaterialInstance(0);
	}
	UpdateLook();

	if (!HasAuthority())
	{
		// Clients only draw what replicates.
		SetActorTickEnabled(false);
		return;
	}

#if !UE_BUILD_SHIPPING
	// Network tests for earlier stages run on the same map without infected.
	if (FParse::Param(FCommandLine::Get(), TEXT("AftermathNoInfected")))
	{
		Destroy();
		return;
	}
#endif

	// Unpossessed characters start in MOVE_None; walk (or fall onto the ground) from the start.
	GetCharacterMovement()->SetDefaultMovementMode();
	Health = Def().Health;
	Home = GetActorLocation();
	WanderWait = FMath::FRandRange(0.5f, 4.f);
	if (UAftermathInfectedManager* M = GetWorld()->GetSubsystem<UAftermathInfectedManager>())
	{
		Manager = M;
		M->Register(this);
	}
	UE_LOG(LogAftermath, Display, TEXT("[Aftermath] InfectedSpawned who=%s type=%s at=%s"),
		*GetName(), AftermathInfectedMath::TypeToString(GetInfectedType()), *GetActorLocation().ToCompactString());
}

void AAftermathInfected::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UAftermathInfectedManager* M = Manager.Get())
	{
		M->Unregister(this);
	}
	Super::EndPlay(EndPlayReason);
}

UAftermathInfectedManager* AAftermathInfected::GetManager() const
{
	return Manager.Get();
}

// ---------------------------------------------------------------------------
// Look (all machines)
// ---------------------------------------------------------------------------

void AAftermathInfected::OnRep_Look()
{
	UpdateLook();
}

void AAftermathInfected::OnRep_Dead()
{
	ApplyDeadVisuals();
	UpdateLook();
}

void AAftermathInfected::UpdateLook()
{
	if (!BodyMaterial)
	{
		return;
	}
	FLinearColor C = Def().Color;
	const EAftermathInfectedState S = GetState();
	if (bDead)
	{
		C = FLinearColor(0.08f, 0.08f, 0.08f);
	}
	else if (S == EAftermathInfectedState::Chase || S == EAftermathInfectedState::Attack)
	{
		// Aggressive: flush red so players can read the threat on grey-box bodies.
		C = FMath::Lerp(C, FLinearColor(0.8f, 0.05f, 0.03f), 0.6f);
	}
	else if (S == EAftermathInfectedState::Alert || S == EAftermathInfectedState::Search)
	{
		C = FMath::Lerp(C, FLinearColor(0.8f, 0.6f, 0.05f), 0.4f);
	}
	BodyMaterial->SetVectorParameterValue(TEXT("Color"), C);
}

void AAftermathInfected::ApplyDeadVisuals()
{
	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->StopMovementImmediately();
		Move->DisableMovement();
	}
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	if (PlaceholderBody)
	{
		PlaceholderBody->SetRelativeRotation(FRotator(0.f, 0.f, 90.f));
		PlaceholderBody->SetRelativeLocation(FVector(0.f, 0.f, -60.f));
	}
	if (PlaceholderFace)
	{
		PlaceholderFace->SetVisibility(false);
	}
}

// ---------------------------------------------------------------------------
// Perception (server)
// ---------------------------------------------------------------------------

FVector AAftermathInfected::EyeLocation() const
{
	return GetActorLocation() + FVector(0.f, 0.f, 70.f);
}

bool AAftermathInfected::HasLineOfSight(const FVector& From, const FVector& To, const AActor* IgnoreOther) const
{
	FCollisionQueryParams Params(SCENE_QUERY_STAT(AftermathInfectedSight), true, this);
	if (IgnoreOther)
	{
		Params.AddIgnoredActor(IgnoreOther);
	}
	FHitResult Hit;
	return !GetWorld()->LineTraceSingleByObjectType(Hit, From, To, SightBlockers(), Params);
}

float AAftermathInfected::VisionRateFor(const AAftermathCharacter& Who, float Light01) const
{
	AftermathInfectedMath::FVisionInput V;
	V.Eye = EyeLocation();
	V.Forward = GetActorForwardVector();
	const float Half = Who.GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	V.Target = Who.GetActorLocation() + FVector(0.f, 0.f, Half * 0.5f);
	V.TargetStance = Who.GetStance();
	V.TargetSpeed = Who.GetVelocity().Size2D();
	V.Light01 = Light01;
	// Only trace when the rules say it could be seen (traces are the expensive part).
	V.bLineOfSight = true;
	if (AftermathInfectedMath::VisionRate(V) <= 0.f)
	{
		return 0.f;
	}
	V.bLineOfSight = HasLineOfSight(V.Eye, V.Target, &Who);
	return AftermathInfectedMath::VisionRate(V);
}

void AAftermathInfected::HearNoise(const FAftermathNoiseEvent& Event)
{
	if (bDead || !HasAuthority())
	{
		return;
	}
	const FVector Eye = EyeLocation();
	const float Dist = FVector::Dist(Event.Location, Eye);
	UAftermathInfectedManager* M = GetManager();
	const float Rain = M ? M->GetRain01() : 0.f;
	// Cheap reject before the occlusion trace.
	if (AftermathInfectedMath::Hearing(Event.Radius, Dist, false, Rain) <= 0.f)
	{
		return;
	}
	const bool bOccluded = !HasLineOfSight(Event.Location, Eye, Event.Instigator.Get());
	const float Loudness = AftermathInfectedMath::Hearing(Event.Radius, Dist, bOccluded, Rain);
	if (Loudness <= 0.f)
	{
		return;
	}
	Awareness = FMath::Min(AftermathInfectedMath::MaxAwareness, Awareness + AftermathInfectedMath::AwarenessFromSound(Loudness));
	bHasInterestPoint = true;
	InterestPoint = Event.Location;
	LastStimulus = NoiseName(Event.Kind);
	if (State == EAftermathInfectedState::Search)
	{
		// Something new to look at: search around it instead.
		SearchCenter = Event.Location;
		PathPoints.Reset();
	}
	bThinkNow = true;
	SetSleeping(false);
	UE_LOG(LogAftermath, Verbose, TEXT("[Aftermath] InfectedHeard who=%s kind=%s loud=%.2f occluded=%d"),
		*GetName(), LastStimulus, Loudness, bOccluded ? 1 : 0);
}

bool AAftermathInfected::HearRoar(const FVector& From, AAftermathCharacter* RoarTarget)
{
	if (bDead || !(State == EAftermathInfectedState::Idle || State == EAftermathInfectedState::Return))
	{
		return false;
	}
	bCalledByOthers = true;
	bHasInterestPoint = true;
	InterestPoint = RoarTarget ? RoarTarget->GetActorLocation() : From;
	Awareness = FMath::Max(Awareness, 0.6f);
	LastStimulus = TEXT("roar");
	bThinkNow = true;
	SetSleeping(false);
	return true;
}

void AAftermathInfected::ServerThink(float Dt, const TArray<AAftermathCharacter*>& Players)
{
	using namespace AftermathInfectedMath;
	if (bDead)
	{
		return;
	}
	++ThinkCount;

	// ---- Vision ----
	const UAftermathInfectedManager* M = GetManager();
	const float Light = M ? M->GetLight01() : 1.f;
	AAftermathCharacter* Best = nullptr;
	float BestRate = 0.f;
	const FVector Me = GetActorLocation();
	for (AAftermathCharacter* P : Players)
	{
		if (!P || P->IsDead() || FVector::DistSquared(P->GetActorLocation(), Me) > FMath::Square(VisionRange * 1.4f))
		{
			continue;
		}
		const float Rate = VisionRateFor(*P, Light);
		if (Rate > BestRate)
		{
			BestRate = Rate;
			Best = P;
		}
	}
	if (bForceSeeTarget && Target.IsValid() && !Target->IsDead())
	{
		// Just got shot or hit by it: we know exactly who and where.
		Best = Target.Get();
		BestRate = FMath::Max(BestRate, 10.f);
	}

	if (Best)
	{
		Awareness = FMath::Min(MaxAwareness, Awareness + BestRate * Dt);
		Target = Best;
		bTargetVisible = true;
		SinceTargetSeen = 0.f;
		LastSeenLocation = Best->GetActorLocation();
		bHasInterestPoint = true;
		InterestPoint = LastSeenLocation;
		if (!bForceSeeTarget)
		{
			LastStimulus = TEXT("sight");
		}
	}
	else
	{
		bTargetVisible = false;
		SinceTargetSeen += Dt;
		Awareness = FMath::Max(0.f, Awareness - AwarenessDecayPerSec * Dt);
	}
	if (Target.IsValid() && Target->IsDead())
	{
		Target.Reset();
		bTargetVisible = false;
	}

	// ---- State machine ----
	FBrainInput In;
	In.State = State;
	In.Awareness = Awareness;
	In.bHasTarget = Target.IsValid();
	In.bTargetVisible = bTargetVisible;
	In.DistToTarget = Target.IsValid() ? FVector::Dist2D(Target->GetActorLocation(), Me) : TNumericLimits<float>::Max();
	In.SinceTargetSeen = SinceTargetSeen;
	In.bHasInterestPoint = bHasInterestPoint;
	In.bReachedInterestPoint = bHasInterestPoint && (FVector::Dist2D(Me, InterestPoint) < 200.f
		|| (HasArrived() && FVector::Dist2D(Me, PathGoal) < 200.f && TimeInState > 1.f));
	In.TimeInState = TimeInState;
	In.bAtHome = FVector::Dist2D(Me, Home) < 250.f;
	In.bCalledByOthers = bCalledByOthers;

	const EAftermathInfectedState Next = NextState(In);
	if (Next != State)
	{
		SetState(Next, bForceSeeTarget ? TEXT("hit") : LastStimulus);
	}
	else if (State == EAftermathInfectedState::Attack && TimeInState >= AttackWindup + AttackRecovery)
	{
		// Still in reach: next swing.
		TimeInState = 0.f;
		bSwingPending = true;
	}
	bCalledByOthers = false;
	bForceSeeTarget = false;

	// ---- Where to go ----
	switch (State)
	{
	case EAftermathInfectedState::Idle:
		if (bAllowWander && HasArrived())
		{
			WanderWait -= Dt;
			if (WanderWait <= 0.f)
			{
				PickWanderPoint(Home, WanderRadius);
				WanderWait = FMath::FRandRange(3.f, 8.f);
			}
		}
		break;
	case EAftermathInfectedState::Alert:
		if (bHasInterestPoint)
		{
			MoveTo(InterestPoint, Awareness >= ChaseThreshold);
		}
		break;
	case EAftermathInfectedState::Chase:
		if (Target.IsValid())
		{
			MoveTo(bTargetVisible ? Target->GetActorLocation() : LastSeenLocation, true);
		}
		break;
	case EAftermathInfectedState::Attack:
		StopMoving();
		break;
	case EAftermathInfectedState::Search:
		if (HasArrived())
		{
			PickWanderPoint(SearchCenter, 500.f);
		}
		break;
	case EAftermathInfectedState::Return:
		MoveTo(Home, false);
		break;
	default:
		break;
	}
}

void AAftermathInfected::SetState(EAftermathInfectedState NewState, const TCHAR* Reason)
{
	using namespace AftermathInfectedMath;
	const EAftermathInfectedState Old = State;
	State = NewState;
	RepState = static_cast<uint8>(NewState);
	TimeInState = 0.f;

	switch (NewState)
	{
	case EAftermathInfectedState::Chase:
		if (UAftermathInfectedManager* M = GetManager())
		{
			const double Now = GetWorld()->GetTimeSeconds();
			if (Now - LastRoarTime > RoarCooldown)
			{
				LastRoarTime = Now;
				M->Roar(this, Target.Get());
			}
		}
		break;
	case EAftermathInfectedState::Attack:
		StopMoving();
		bSwingPending = true;
		break;
	case EAftermathInfectedState::Search:
		SearchCenter = Old == EAftermathInfectedState::Chase ? LastSeenLocation : (bHasInterestPoint ? InterestPoint : GetActorLocation());
		bHasInterestPoint = false;
		PathPoints.Reset();
		break;
	case EAftermathInfectedState::Return:
		Target.Reset();
		bHasInterestPoint = false;
		Awareness = FMath::Min(Awareness, AlertThreshold * 0.5f);
		break;
	case EAftermathInfectedState::Idle:
		bHasInterestPoint = false;
		StopMoving();
		break;
	default:
		break;
	}

	UE_LOG(LogAftermath, Display, TEXT("[Aftermath] InfectedState who=%s type=%s from=%s to=%s reason=%s target=%s awareness=%.2f"),
		*GetName(), TypeToString(GetInfectedType()), StateToString(Old), StateToString(NewState), Reason,
		*GetNameSafe(Target.Get()), Awareness);
	UpdateLook();
}

// ---------------------------------------------------------------------------
// Damage (server)
// ---------------------------------------------------------------------------

FAftermathHitNotify AAftermathInfected::ServerApplyHit(EAftermathHitZone Zone, float Damage, const FVector& From, const FVector& Impact, AActor* Attacker)
{
	FAftermathHitNotify Notify;
	Notify.ImpactPoint = Impact;
	Notify.FromLocation = From;
	Notify.Zone = Zone;
	if (bDead || !HasAuthority())
	{
		return Notify;
	}
	const float Final = AftermathInfectedMath::DamageAfterArmor(Def(), Zone == EAftermathHitZone::Head, Zone == EAftermathHitZone::Torso, Damage);
	Health -= Final;
	Notify.Damage = Final;

	if (AAftermathCharacter* Survivor = Cast<AAftermathCharacter>(Attacker))
	{
		// Being hurt tells you exactly who did it.
		Target = Survivor;
		bForceSeeTarget = true;
		LastSeenLocation = Survivor->GetActorLocation();
		Awareness = AftermathInfectedMath::MaxAwareness;
		bThinkNow = true;
		SetSleeping(false);
	}

	UE_LOG(LogAftermath, Display, TEXT("[Aftermath] InfectedHit victim=%s type=%s zone=%s damage=%.0f health=%.0f by=%s"),
		*GetName(), AftermathInfectedMath::TypeToString(GetInfectedType()), ZoneText(Zone), Final, FMath::Max(0.f, Health), *GetNameSafe(Attacker));

	if (Health <= 0.f)
	{
		Die(Attacker, Zone);
		Notify.bKilled = true;
	}
	return Notify;
}

void AAftermathInfected::Die(AActor* Killer, EAftermathHitZone Zone)
{
	if (bDead)
	{
		return;
	}
	const EAftermathInfectedState Old = State;
	bDead = true;
	State = EAftermathInfectedState::Dead;
	RepState = static_cast<uint8>(State);
	PathPoints.Reset();
	ApplyDeadVisuals();
	UpdateLook();
	SetLifeSpan(120.f);
	ForceNetUpdate();
	UE_LOG(LogAftermath, Display, TEXT("[Aftermath] InfectedKilled victim=%s type=%s killer=%s zone=%s was=%s"),
		*GetName(), AftermathInfectedMath::TypeToString(GetInfectedType()), *GetNameSafe(Killer), ZoneText(Zone),
		AftermathInfectedMath::StateToString(Old));
}

// ---------------------------------------------------------------------------
// Movement (server)
// ---------------------------------------------------------------------------

void AAftermathInfected::MoveTo(const FVector& Goal, bool bRun)
{
	GetCharacterMovement()->MaxWalkSpeed = bRun ? Def().RunSpeed : Def().WalkSpeed;
	const double Now = GetWorld()->GetTimeSeconds();
	// Re-path only when the goal moved noticeably (chasing) or the old path ran out.
	if (!HasArrived() && FVector::Dist2D(Goal, PathGoal) < 100.f)
	{
		return;
	}
	if (HasArrived() && FVector::Dist2D(Goal, PathGoal) < 100.f && Now - LastPathTime < 1.0)
	{
		return;
	}
	PathGoal = Goal;
	LastPathTime = Now;
	PathPoints.Reset();
	PathIndex = 0;
	StuckTime = 0.f;

	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	ANavigationData* NavData = Nav ? Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate) : nullptr;
	if (NavData)
	{
		FPathFindingQuery Query(this, *NavData, GetNavAgentLocation(), Goal);
		Query.SetAllowPartialPaths(true);
		const FPathFindingResult R = Nav->FindPathSync(Query);
		const ARecastNavMesh* RecastForLog = Cast<ARecastNavMesh>(NavData);
		// Diagnose a few failures, but only once tiles exist (failures before the first build say nothing).
		static int32 LoggedFailures = 0;
		if (!R.IsSuccessful() && LoggedFailures < 5 && RecastForLog && RecastForLog->GetNumActiveTiles() > 0)
		{
			++LoggedFailures;
			// Diagnose the first few failures (start/goal not on the mesh, or no tiles here yet).
			FNavLocation StartOn, GoalOn;
			const bool bStart = Nav->ProjectPointToNavigation(GetNavAgentLocation(), StartOn, FVector(200.f, 200.f, 500.f));
			const bool bGoal = Nav->ProjectPointToNavigation(Goal, GoalOn, FVector(200.f, 200.f, 500.f));
			const FBox NavBounds = NavData->GetBounds();
			const ARecastNavMesh* Recast = Cast<ARecastNavMesh>(NavData);
			// Tile pool capacity vs tiles that really hold polygons, plus the tile under our feet.
			const int32 Capacity = Recast ? Recast->GetNavMeshTilesCount() : -1;
			const int32 Active = Recast ? Recast->GetNumActiveTiles() : -1;
			int32 TileX = -1, TileY = -1, HereTiles = 0, HerePolys = 0;
			if (Recast && Recast->GetNavMeshTileXY(GetNavAgentLocation(), TileX, TileY))
			{
				TArray<FNavTileRef> Refs;
				Recast->GetNavMeshTilesAt(TileX, TileY, Refs);
				HereTiles = Refs.Num();
				FString PolyAt;
				for (const FNavTileRef& Ref : Refs)
				{
					TArray<FNavPoly> Polys;
					Recast->GetPolysInTile(Ref, Polys);
					HerePolys += Polys.Num();
					for (int32 p = 0; p < FMath::Min(Polys.Num(), 3); ++p)
					{
						PolyAt += Polys[p].Center.ToCompactString() + TEXT(" ");
					}
				}
				UE_LOG(LogAftermath, Display, TEXT("[Aftermath] InfectedPathFail polys_at=%s"), *PolyAt);
			}
			UE_LOG(LogAftermath, Display, TEXT("[Aftermath] InfectedPathFail who=%s result=%d start=%s on_mesh=%d goal=%s on_mesh=%d nav_center=%s nav_extent=%s tile_pool=%d active_tiles=%d here_tile=(%d,%d) here_layers=%d here_polys=%d"),
				*GetName(), static_cast<int32>(R.Result), *GetNavAgentLocation().ToCompactString(), bStart ? 1 : 0,
				*Goal.ToCompactString(), bGoal ? 1 : 0,
				NavBounds.IsValid ? *NavBounds.GetCenter().ToCompactString() : TEXT("none"),
				NavBounds.IsValid ? *NavBounds.GetExtent().ToCompactString() : TEXT("none"),
				Capacity, Active, TileX, TileY, HereTiles, HerePolys);
		}
		if (R.IsSuccessful() && R.Path.IsValid())
		{
			for (const FNavPathPoint& P : R.Path->GetPathPoints())
			{
				PathPoints.Add(P.Location);
			}
			// First point is where we stand.
			if (PathPoints.Num() > 1)
			{
				PathIndex = 1;
			}
			++NavPathsFound;
			return;
		}
	}
	// No navmesh here (tests, or off-mesh): walk straight at it. Walls and closed doors still stop us.
	++DirectPaths;
	PathPoints.Add(Goal);
}

void AAftermathInfected::StopMoving()
{
	PathPoints.Reset();
	PathIndex = 0;
	PathGoal = FVector(1e9);
}

void AAftermathInfected::PickWanderPoint(const FVector& Center, float Radius)
{
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	FNavLocation Loc;
	if (Nav && Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate) && Nav->GetRandomReachablePointInRadius(Center, Radius, Loc))
	{
		MoveTo(Loc.Location, false);
		return;
	}
	const FVector2D Off = FMath::RandPointInCircle(Radius);
	MoveTo(Center + FVector(Off.X, Off.Y, 0.f), false);
}

void AAftermathInfected::FaceTowards(const FVector& Where, float DeltaSeconds)
{
	const FVector To = Where - GetActorLocation();
	if (To.SizeSquared2D() < 1.f)
	{
		return;
	}
	const FRotator Want(0.f, To.Rotation().Yaw, 0.f);
	SetActorRotation(FMath::RInterpConstantTo(GetActorRotation(), Want, DeltaSeconds, 540.f));
}

void AAftermathInfected::FollowPath(float DeltaSeconds)
{
	if (HasArrived())
	{
		StuckTime = 0.f;
		return;
	}
	const FVector Me = GetActorLocation();
	// Chasing: stop just short of the target instead of walking into it.
	if (State == EAftermathInfectedState::Chase && Target.IsValid()
		&& FVector::Dist2D(Target->GetActorLocation(), Me) < AftermathInfectedMath::AttackReach * 0.7f)
	{
		FaceTowards(Target->GetActorLocation(), DeltaSeconds);
		return;
	}
	const FVector P = PathPoints[PathIndex];
	const FVector Delta = FVector(P.X - Me.X, P.Y - Me.Y, 0.f);
	const bool bLast = PathIndex == PathPoints.Num() - 1;
	if (Delta.Size() < (bLast ? 80.f : 50.f))
	{
		++PathIndex;
		return;
	}
	AddMovementInput(Delta.GetSafeNormal(), 1.f);
	FaceTowards(P, DeltaSeconds);

	// Stuck against a wall or a closed door: give up on this path (the brain picks a new one).
	if (GetVelocity().Size2D() < 20.f)
	{
		StuckTime += DeltaSeconds;
		if (StuckTime > 1.5f)
		{
			StuckTime = 0.f;
			StopMoving();
			LastPathTime = GetWorld()->GetTimeSeconds();
		}
	}
	else
	{
		StuckTime = 0.f;
	}
}

void AAftermathInfected::UpdateAttack(float DeltaSeconds)
{
	if (State != EAftermathInfectedState::Attack)
	{
		return;
	}
	AAftermathCharacter* T = Target.Get();
	if (T)
	{
		FaceTowards(T->GetActorLocation(), DeltaSeconds);
	}
	if (!bSwingPending || TimeInState < AftermathInfectedMath::AttackWindup)
	{
		return;
	}
	bSwingPending = false;

	const FAftermathInfectedTypeDef& D = Def();
	// The swing connects only if the target is still in reach, in front, and not behind a wall.
	bool bHit = false;
	EAftermathHitZone Zone = EAftermathHitZone::None;
	if (T && !T->IsDead())
	{
		const FVector To = T->GetActorLocation() - GetActorLocation();
		const bool bInReach = To.Size2D() <= AftermathInfectedMath::AttackReach + 20.f;
		const bool bInFront = FVector::DotProduct(To.GetSafeNormal2D(), GetActorForwardVector()) > 0.5f;
		if (bInReach && bInFront && HasLineOfSight(EyeLocation(), T->GetActorLocation(), T))
		{
			bHit = true;
			Zone = RollSwingZone();
			UAftermathCombatComponent::ApplyHitToSurvivor(*T, Zone, D.AttackDamage, D.AttackBlood, D.AttackCutChance,
				EyeLocation(), T->GetActorLocation(), this);
		}
	}
	UE_LOG(LogAftermath, Display, TEXT("[Aftermath] InfectedAttack by=%s type=%s target=%s hit=%d zone=%s"),
		*GetName(), AftermathInfectedMath::TypeToString(GetInfectedType()), *GetNameSafe(T), bHit ? 1 : 0, ZoneText(Zone));
}

void AAftermathInfected::SetSleeping(bool bSleep)
{
	if (bSleeping == bSleep || bDead)
	{
		return;
	}
	bSleeping = bSleep;
	SetActorTickEnabled(!bSleep);
	GetCharacterMovement()->SetComponentTickEnabled(!bSleep);
	// Sleeping infected don't need a navmesh around them (saves tile generation on a big map).
	if (NavInvoker)
	{
		NavInvoker->SetActive(!bSleep);
	}
	if (bSleep)
	{
		StopMoving();
		GetCharacterMovement()->StopMovementImmediately();
	}
}

void AAftermathInfected::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority() || bDead || bSleeping)
	{
		return;
	}
	TimeInState += DeltaSeconds;
	UpdateAttack(DeltaSeconds);
	if (State != EAftermathInfectedState::Attack)
	{
		FollowPath(DeltaSeconds);
	}
}
