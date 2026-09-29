#include "Movement/AftermathMovementComponent.h"

#include "Aftermath.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "Player/AftermathCharacter.h"
#include "Survival/AftermathSurvivalComponent.h"

const FName UAftermathMovementComponent::VaultInstanceName(TEXT("AftermathVault"));

// ============================================================================
// Vault root motion source
// ============================================================================

FAftermathRootMotionSource_Vault::FAftermathRootMotionSource_Vault()
{
	// Priority above normal gameplay forces; override so gravity/input don't fight the path.
	Priority = 500;
	AccumulateMode = ERootMotionAccumulateMode::Override;
	// Land with zero horizontal speed so the character doesn't slide off the landing spot.
	FinishVelocityParams.Mode = ERootMotionFinishVelocityMode::SetVelocity;
	FinishVelocityParams.SetVelocity = FVector::ZeroVector;
}

void FAftermathRootMotionSource_Vault::SetPath(const FAftermathVaultPath& Path)
{
	PathStart = Path.Start;
	PathForward = Path.Forward;
	PathDistances = FVector(Path.RiseEnd, Path.HoldEnd, Path.Length);
	PathHeights = FVector(Path.StartZ, Path.PeakZ, Path.EndZ);
}

FAftermathVaultPath FAftermathRootMotionSource_Vault::GetPath() const
{
	FAftermathVaultPath Path;
	Path.Start = PathStart;
	Path.Forward = PathForward;
	Path.RiseEnd = PathDistances.X;
	Path.HoldEnd = PathDistances.Y;
	Path.Length = PathDistances.Z;
	Path.StartZ = PathHeights.X;
	Path.PeakZ = PathHeights.Y;
	Path.EndZ = PathHeights.Z;
	return Path;
}

FRootMotionSource* FAftermathRootMotionSource_Vault::Clone() const
{
	return new FAftermathRootMotionSource_Vault(*this);
}

bool FAftermathRootMotionSource_Vault::Matches(const FRootMotionSource* Other) const
{
	if (!FRootMotionSource::Matches(Other))
	{
		return false;
	}
	const FAftermathRootMotionSource_Vault* O = static_cast<const FAftermathRootMotionSource_Vault*>(Other);
	return FVector::PointsAreNear(PathStart, O->PathStart, 1.f)
		&& FVector::PointsAreNear(PathDistances, O->PathDistances, 1.f)
		&& FVector::PointsAreNear(PathHeights, O->PathHeights, 1.f);
}

bool FAftermathRootMotionSource_Vault::UpdateStateFrom(const FRootMotionSource* SourceToTakeStateFrom, bool bMarkForSimulatedCatchup)
{
	// Time is the only state and the base class copies it.
	return FRootMotionSource::UpdateStateFrom(SourceToTakeStateFrom, bMarkForSimulatedCatchup);
}

void FAftermathRootMotionSource_Vault::PrepareRootMotion(float SimulationTime, float MovementTickTime,
	const ACharacter& Character, const UCharacterMovementComponent& MoveComponent)
{
	RootMotionParams.Clear();

	if (Duration <= UE_SMALL_NUMBER || MovementTickTime <= UE_SMALL_NUMBER)
	{
		return;
	}

	const FAftermathVaultPath Path = GetPath();
	const float Total = Path.TotalPathLength();
	const float TargetFraction = FMath::Clamp((GetTime() + SimulationTime) / Duration, 0.f, 1.f);
	const FVector Target = Path.Evaluate(Total * TargetFraction);

	// Velocity that reaches the path point this tick. Steering toward the absolute point (not a delta)
	// self-corrects any drift from collision or a mispredicted start.
	const FVector Force = (Target - Character.GetActorLocation()) / MovementTickTime;

	FTransform NewTransform(Force);
	RootMotionParams.Set(NewTransform);

	SetTime(GetTime() + SimulationTime);
}

bool FAftermathRootMotionSource_Vault::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	if (!FRootMotionSource::NetSerialize(Ar, Map, bOutSuccess))
	{
		return false;
	}
	Ar << PathStart;
	Ar << PathForward;
	Ar << PathDistances;
	Ar << PathHeights;
	bOutSuccess = true;
	return true;
}

UScriptStruct* FAftermathRootMotionSource_Vault::GetScriptStruct() const
{
	return FAftermathRootMotionSource_Vault::StaticStruct();
}

FString FAftermathRootMotionSource_Vault::ToSimpleString() const
{
	return FString::Printf(TEXT("[ID:%u]FAftermathRootMotionSource_Vault %s"), LocalID, *InstanceName.GetPlainNameString());
}

// ============================================================================
// Movement component
// ============================================================================

UAftermathMovementComponent::UAftermathMovementComponent()
{
	bWantsSprint = false;
	bWantsWalk = false;
	bWantsProne = false;
	bWantsVault = false;

	NavAgentProps.bCanCrouch = true;
	bCanWalkOffLedgesWhenCrouching = true;
	SetCrouchedHalfHeight(60.f);
	MaxWalkSpeed = Speeds.Jog;
	MaxWalkSpeedCrouched = Speeds.Crouch;
	MaxAcceleration = 1600.f;
	BrakingDecelerationWalking = 1400.f;
	GroundFriction = 8.f;
	JumpZVelocity = 420.f;
	AirControl = 0.15f;
}

float UAftermathMovementComponent::GetDefaultHalfHeight() const
{
	const ACharacter* DefaultChar = CharacterOwner ? CharacterOwner->GetClass()->GetDefaultObject<ACharacter>() : nullptr;
	return DefaultChar ? DefaultChar->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() : 90.f;
}

bool UAftermathMovementComponent::IsCapsuleAtHalfHeight(float HalfHeight) const
{
	const UCapsuleComponent* Capsule = CharacterOwner ? CharacterOwner->GetCapsuleComponent() : nullptr;
	return Capsule && FMath::IsNearlyEqual(Capsule->GetUnscaledCapsuleHalfHeight(), FMath::Max(HalfHeight, Capsule->GetUnscaledCapsuleRadius()), 0.5f);
}

EAftermathStance UAftermathMovementComponent::GetStance() const
{
	if (!CharacterOwner)
	{
		return EAftermathStance::Standing;
	}
	const float Current = CharacterOwner->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
	if (Current <= ProneHalfHeight + 0.5f || IsCapsuleAtHalfHeight(ProneHalfHeight))
	{
		return EAftermathStance::Prone;
	}
	if (Current < GetDefaultHalfHeight() - 0.5f)
	{
		return EAftermathStance::Crouched;
	}
	return EAftermathStance::Standing;
}

bool UAftermathMovementComponent::IsProne() const
{
	return GetStance() == EAftermathStance::Prone;
}

bool UAftermathMovementComponent::IsVaulting() const
{
	// GetRootMotionSource is non-const in the engine; the lookup itself doesn't mutate.
	const TSharedPtr<FRootMotionSource> Source = const_cast<UAftermathMovementComponent*>(this)->GetRootMotionSource(VaultInstanceName);
	return Source.IsValid() && !Source->Status.HasFlag(ERootMotionSourceStatusFlags::Finished);
}

void UAftermathMovementComponent::ApplyReplicatedStance(bool bCrouched, bool bProne)
{
	if (!CharacterOwner || CharacterOwner->GetLocalRole() != ROLE_SimulatedProxy)
	{
		return;
	}

	if (!bCrouched)
	{
		bWantsToCrouch = false;
		UnCrouch(true);
		return;
	}

	const float Target = bProne ? ProneHalfHeight : GetCrouchedHalfHeight();
	if (IsCapsuleAtHalfHeight(Target))
	{
		return;
	}

	// Crouch(true) on a proxy restores the default capsule first, then shrinks to CrouchedHalfHeight,
	// so switching directly between prone and crouch works without an intermediate stand.
	bWantsToCrouch = true;
	CrouchToHalfHeight(Target, true);
}

void UAftermathMovementComponent::CrouchToHalfHeight(float TargetHalfHeight, bool bClientSimulation)
{
	// The engine crouch path (encroachment test, base-location handling, mesh offset, bIsCrouched)
	// only knows one crouched height. Point it at the stance we want for this call, then restore.
	const float Saved = GetCrouchedHalfHeight();
	SetCrouchedHalfHeight(TargetHalfHeight);
	Crouch(bClientSimulation);
	SetCrouchedHalfHeight(Saved);
}

bool UAftermathMovementComponent::IsSprinting() const
{
	return IsMovingOnGround() && GetStance() == EAftermathStance::Standing && bWantsSprint
		&& Velocity.SizeSquared2D() > FMath::Square(Speeds.Jog + 10.f);
}

float UAftermathMovementComponent::GetMaxSpeed() const
{
	if (MovementMode != MOVE_Walking && MovementMode != MOVE_NavWalking)
	{
		return Super::GetMaxSpeed();
	}

	// Sprint only when moving roughly forward relative to where the body faces.
	bool bMovingForward = true;
	if (CharacterOwner)
	{
		const FVector Accel = GetCurrentAcceleration().GetSafeNormal2D();
		bMovingForward = Accel.IsNearlyZero()
			|| FVector::DotProduct(Accel, CharacterOwner->GetActorForwardVector().GetSafeNormal2D()) > 0.7f;
	}

	// Survival limits (broken legs, stamina, weight, blood). Vitals replicate to the owner, so the
	// predicting client and the server read the same values; brief lag is fixed by normal corrections.
	bool bSprintAllowed = bWantsSprint;
	float Multiplier = 1.f;
	if (const AAftermathCharacter* Survivor = Cast<AAftermathCharacter>(CharacterOwner))
	{
		if (const UAftermathSurvivalComponent* Survival = Survivor->GetSurvival())
		{
			bSprintAllowed = bSprintAllowed && Survival->CanSprint();
			Multiplier = Survival->GetSpeedMultiplier();
		}
	}

	return Multiplier * AftermathMath::ComputeMaxGroundSpeed(Speeds, GetStance(), bSprintAllowed, bWantsWalk, bMovingForward);
}

float UAftermathMovementComponent::GetMaxAcceleration() const
{
	// Heavier feel when low to the ground.
	switch (GetStance())
	{
	case EAftermathStance::Prone:    return MaxAcceleration * 0.5f;
	case EAftermathStance::Crouched: return MaxAcceleration * 0.8f;
	default:                         return MaxAcceleration;
	}
}

bool UAftermathMovementComponent::CanAttemptJump() const
{
	// Space is shared with vault; no jumping while low or mid-vault.
	return Super::CanAttemptJump() && !bWantsProne && GetStance() == EAftermathStance::Standing && !IsVaulting();
}

bool UAftermathMovementComponent::CanCrouchInCurrentState() const
{
	return Super::CanCrouchInCurrentState() && !IsVaulting();
}

bool UAftermathMovementComponent::TrySetStanceHalfHeight(float TargetHalfHeight)
{
	if (IsCapsuleAtHalfHeight(TargetHalfHeight))
	{
		return true;
	}

	const float DefaultHalf = GetDefaultHalfHeight();

	if (TargetHalfHeight >= DefaultHalf - 0.5f)
	{
		UnCrouch(false);
		return IsCapsuleAtHalfHeight(DefaultHalf);
	}

	// Going lower always fits; rising from prone to crouch is handled by Crouch(), which grows
	// the capsule and cancels (restoring the old size) if it would encroach.
	CrouchToHalfHeight(TargetHalfHeight, false);
	return IsCapsuleAtHalfHeight(TargetHalfHeight);
}

void UAftermathMovementComponent::UpdateCharacterStateBeforeMovement(float DeltaSeconds)
{
	if (!CharacterOwner || CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy)
	{
		// Proxies take stance from replicated bIsCrouched + capsule size.
		Super::UpdateCharacterStateBeforeMovement(DeltaSeconds);
		return;
	}

	// ---- Vault (consumed every move so a held key doesn't retrigger) ----
	if (bWantsVault)
	{
		bWantsVault = false;
		if (!IsVaulting() && IsMovingOnGround())
		{
			const FAftermathVaultQuery Query = QueryVault();
			if (Query.bCanVault)
			{
				StartVault(Query.Path);
				// Stable marker for network tests (server side only, and only for real remote requests).
				if (CharacterOwner->HasAuthority() && !CharacterOwner->IsLocallyControlled() && !CharacterOwner->bClientUpdating)
				{
					UE_LOG(LogAftermath, Display, TEXT("[Aftermath] VaultStarted by=%s height=%.0f"),
						*CharacterOwner->GetName(), Query.ObstacleHeight);
				}
			}
			else if (CharacterOwner->HasAuthority() && !CharacterOwner->IsLocallyControlled())
			{
				UE_LOG(LogAftermath, Verbose, TEXT("[Aftermath] Vault rejected: %s"), *Query.FailReason);
			}
		}
	}

	// ---- Stance ----
	if (!IsVaulting() && CanCrouchInCurrentState())
	{
		if (bWantsProne)
		{
			TrySetStanceHalfHeight(ProneHalfHeight);
		}
		else if (bWantsToCrouch)
		{
			TrySetStanceHalfHeight(GetCrouchedHalfHeight());
		}
		else if (GetStance() != EAftermathStance::Standing)
		{
			// Try to stand; if blocked, stay as low as needed (prone -> try crouch).
			if (!TrySetStanceHalfHeight(GetDefaultHalfHeight()) && GetStance() == EAftermathStance::Prone)
			{
				TrySetStanceHalfHeight(GetCrouchedHalfHeight());
			}
		}
	}
	// Deliberately not calling Super: it would uncrouch whenever bWantsToCrouch is false, fighting prone.

	// Authority publishes prone for other players' proxies (bIsCrouched alone can't express it).
	if (CharacterOwner->HasAuthority())
	{
		if (AAftermathCharacter* Survivor = Cast<AAftermathCharacter>(CharacterOwner))
		{
			Survivor->SetReplicatedProne(GetStance() == EAftermathStance::Prone);
		}
	}
}

void UAftermathMovementComponent::UpdateCharacterStateAfterMovement(float DeltaSeconds)
{
	const bool bVaulting = IsVaulting();

	// Vault ran in MOVE_Flying; once the path is done hand control back to normal physics.
	// Falling finds the floor on the next tick (we land ~2cm above it).
	if (!bVaulting && MovementMode == MOVE_Flying && CharacterOwner && CharacterOwner->GetLocalRole() != ROLE_SimulatedProxy)
	{
		SetMovementMode(MOVE_Falling);
	}

	// Base version force-uncrouches when crouching isn't allowed (e.g. swimming). Keep that, but not mid-vault.
	if (!bVaulting)
	{
		Super::UpdateCharacterStateAfterMovement(DeltaSeconds);
	}
}

FAftermathVaultQuery UAftermathMovementComponent::QueryVault() const
{
	FAftermathVaultQuery Out;

	if (!CharacterOwner || !UpdatedComponent || !GetWorld())
	{
		Out.FailReason = TEXT("no owner");
		return Out;
	}
	if (GetStance() != EAftermathStance::Standing)
	{
		Out.FailReason = TEXT("must be standing");
		return Out;
	}

	const UCapsuleComponent* Capsule = CharacterOwner->GetCapsuleComponent();
	const float Radius = Capsule->GetScaledCapsuleRadius();
	const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	const FVector Center = UpdatedComponent->GetComponentLocation();
	const float FeetZ = Center.Z - HalfHeight;
	const FVector Forward = CharacterOwner->GetActorForwardVector().GetSafeNormal2D();

	FCollisionQueryParams Params(SCENE_QUERY_STAT(AftermathVault), false, CharacterOwner);
	FCollisionResponseParams Response;
	InitCollisionParams(Params, Response);
	const ECollisionChannel Channel = UpdatedComponent->GetCollisionObjectType();
	const UWorld* World = GetWorld();

	// 1) Front face: horizontal line trace at knee height.
	const float KneeZ = FeetZ + VaultMinHeight * 0.75f;
	const FVector FrontStart(Center.X, Center.Y, KneeZ);
	const FVector FrontEnd = FrontStart + Forward * (Radius + VaultReach);
	FHitResult FrontHit;
	if (!World->LineTraceSingleByChannel(FrontHit, FrontStart, FrontEnd, Channel, Params, Response))
	{
		Out.FailReason = TEXT("nothing ahead");
		return Out;
	}
	if (FMath::Abs(FrontHit.ImpactNormal.Z) > 0.3f)
	{
		Out.FailReason = TEXT("not a wall");
		return Out;
	}
	const float FrontDist = FVector::DotProduct(FrontHit.ImpactPoint - Center, Forward);

	// 2) Top: trace down just behind the front face to find the obstacle top.
	const FVector TopProbeXY = FVector(Center.X, Center.Y, 0.f) + Forward * (FrontDist + 8.f);
	const float MaxTopZ = FeetZ + VaultMaxHeight;
	FHitResult TopHit;
	const FVector TopStart(TopProbeXY.X, TopProbeXY.Y, MaxTopZ + 30.f);
	const FVector TopEnd(TopProbeXY.X, TopProbeXY.Y, FeetZ);
	if (!World->LineTraceSingleByChannel(TopHit, TopStart, TopEnd, Channel, Params, Response) || TopHit.bStartPenetrating)
	{
		Out.FailReason = TEXT("no top");
		return Out;
	}
	const float TopZ = TopHit.ImpactPoint.Z;
	Out.ObstacleHeight = TopZ - FeetZ;
	if (TopZ > MaxTopZ)
	{
		Out.FailReason = TEXT("too high");
		return Out;
	}
	if (Out.ObstacleHeight < VaultMinHeight)
	{
		Out.FailReason = TEXT("too low (step over it)");
		return Out;
	}

	// 3) Back face: trace backward from beyond the max depth, just under the top, to find where the obstacle ends.
	const float BackProbeZ = TopZ - 5.f;
	const FVector BackStart = FVector(Center.X, Center.Y, BackProbeZ) + Forward * (FrontDist + VaultMaxDepth + 10.f);
	const FVector BackEnd = FVector(Center.X, Center.Y, BackProbeZ) + Forward * FrontDist;
	FHitResult BackHit;
	if (!World->LineTraceSingleByChannel(BackHit, BackStart, BackEnd, Channel, Params, Response))
	{
		Out.FailReason = TEXT("no back face");
		return Out;
	}
	if (BackHit.bStartPenetrating)
	{
		Out.FailReason = TEXT("too deep");
		return Out;
	}
	const float BackDist = FVector::DotProduct(BackHit.ImpactPoint - Center, Forward);
	if (BackDist - FrontDist > VaultMaxDepth)
	{
		Out.FailReason = TEXT("too deep");
		return Out;
	}

	// 4) Landing: find ground behind the obstacle.
	const float LandDist = FAftermathVaultPath::LandingDistance(BackDist, Radius);
	const FVector LandXY = FVector(Center.X, Center.Y, 0.f) + Forward * LandDist;
	FHitResult LandHit;
	const FVector LandStart(LandXY.X, LandXY.Y, TopZ + 10.f);
	const FVector LandEnd(LandXY.X, LandXY.Y, FeetZ - 150.f);
	if (!World->LineTraceSingleByChannel(LandHit, LandStart, LandEnd, Channel, Params, Response) || LandHit.bStartPenetrating
		|| LandHit.ImpactNormal.Z < GetWalkableFloorZ())
	{
		Out.FailReason = TEXT("no landing");
		return Out;
	}
	const float EndCenterZ = LandHit.ImpactPoint.Z + HalfHeight + 2.f;

	const FAftermathVaultPath Path = FAftermathVaultPath::Build(Center, Forward, FrontDist, BackDist, TopZ, EndCenterZ,
		Radius, HalfHeight, VaultClearance);

	// 5) Clearance: the capsule must fit at the peak above the obstacle and at the landing spot.
	const FCollisionShape Shape = GetPawnCapsuleCollisionShape(SHRINK_None);
	const FVector PeakPoint = Path.Evaluate(FMath::Sqrt(FMath::Square(Path.RiseEnd) + FMath::Square(Path.PeakZ - Path.StartZ)));
	const FVector LandPoint = Path.Evaluate(Path.TotalPathLength());
	if (World->OverlapBlockingTestByChannel(PeakPoint, FQuat::Identity, Channel, Shape, Params, Response))
	{
		Out.FailReason = TEXT("no headroom");
		return Out;
	}
	if (World->OverlapBlockingTestByChannel(LandPoint, FQuat::Identity, Channel, Shape, Params, Response))
	{
		Out.FailReason = TEXT("landing blocked");
		return Out;
	}

	Out.bCanVault = true;
	Out.Path = Path;
	return Out;
}

void UAftermathMovementComponent::StartVault(const FAftermathVaultPath& Path)
{
	TSharedPtr<FAftermathRootMotionSource_Vault> Source = MakeShared<FAftermathRootMotionSource_Vault>();
	Source->InstanceName = VaultInstanceName;
	Source->SetPath(Path);
	Source->Duration = FMath::Max(0.15f, Path.TotalPathLength() / FMath::Max(VaultSpeed, 1.f));
	ApplyRootMotionSource(Source);

	// Root motion drives position; flying mode ignores gravity and floor snapping during the arc.
	SetMovementMode(MOVE_Flying);
}

void UAftermathMovementComponent::UpdateFromCompressedFlags(uint8 Flags)
{
	Super::UpdateFromCompressedFlags(Flags);

	bWantsSprint = (Flags & FSavedMove_Aftermath::FLAG_Sprint) != 0;
	bWantsWalk = (Flags & FSavedMove_Aftermath::FLAG_Walk) != 0;
	bWantsProne = (Flags & FSavedMove_Aftermath::FLAG_Prone) != 0;
	// Vault is an edge-triggered request: only ever set here, cleared once consumed.
	if ((Flags & FSavedMove_Aftermath::FLAG_Vault) != 0)
	{
		bWantsVault = true;
	}
}

FNetworkPredictionData_Client* UAftermathMovementComponent::GetPredictionData_Client() const
{
	if (!ClientPredictionData)
	{
		UAftermathMovementComponent* MutableThis = const_cast<UAftermathMovementComponent*>(this);
		MutableThis->ClientPredictionData = new FNetworkPredictionData_Client_Aftermath(*this);
	}
	return ClientPredictionData;
}

// ============================================================================
// Saved move
// ============================================================================

void FSavedMove_Aftermath::Clear()
{
	Super::Clear();
	bSavedWantsSprint = false;
	bSavedWantsWalk = false;
	bSavedWantsProne = false;
	bSavedWantsVault = false;
}

uint8 FSavedMove_Aftermath::GetCompressedFlags() const
{
	uint8 Result = Super::GetCompressedFlags();
	if (bSavedWantsSprint) { Result |= FLAG_Sprint; }
	if (bSavedWantsWalk)   { Result |= FLAG_Walk; }
	if (bSavedWantsProne)  { Result |= FLAG_Prone; }
	if (bSavedWantsVault)  { Result |= FLAG_Vault; }
	return Result;
}

bool FSavedMove_Aftermath::CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* InCharacter, float MaxDelta) const
{
	const FSavedMove_Aftermath* Other = static_cast<const FSavedMove_Aftermath*>(NewMove.Get());
	// Never merge away a vault request.
	if (bSavedWantsVault || Other->bSavedWantsVault)
	{
		return false;
	}
	// Base already compares compressed flags, which include ours.
	return Super::CanCombineWith(NewMove, InCharacter, MaxDelta);
}

void FSavedMove_Aftermath::SetMoveFor(ACharacter* C, float InDeltaTime, FVector const& NewAccel, FNetworkPredictionData_Client_Character& ClientData)
{
	Super::SetMoveFor(C, InDeltaTime, NewAccel, ClientData);

	if (const UAftermathMovementComponent* Move = Cast<UAftermathMovementComponent>(C->GetCharacterMovement()))
	{
		bSavedWantsSprint = Move->bWantsSprint;
		bSavedWantsWalk = Move->bWantsWalk;
		bSavedWantsProne = Move->bWantsProne;
		bSavedWantsVault = Move->bWantsVault;
	}
}

void FSavedMove_Aftermath::PrepMoveFor(ACharacter* C)
{
	Super::PrepMoveFor(C);

	if (UAftermathMovementComponent* Move = Cast<UAftermathMovementComponent>(C->GetCharacterMovement()))
	{
		Move->bWantsSprint = bSavedWantsSprint;
		Move->bWantsWalk = bSavedWantsWalk;
		Move->bWantsProne = bSavedWantsProne;
		Move->bWantsVault = bSavedWantsVault;
	}
}
