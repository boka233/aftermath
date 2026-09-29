#include "Player/AftermathCharacter.h"

#include "Aftermath.h"
#include "Camera/CameraComponent.h"
#include "Combat/AftermathCombatComponent.h"
#include "Components/CapsuleComponent.h"
#include "Core/AftermathGameMode.h"
#include "Debug/AftermathTestScript.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/SpringArmComponent.h"
#include "Interaction/AftermathInteractable.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Items/AftermathInventoryComponent.h"
#include "Items/AftermathItemActions.h"
#include "Items/AftermathItemDatabase.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Movement/AftermathMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "Survival/AftermathSurvivalComponent.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "AftermathCharacter"

AAftermathCharacter::AAftermathCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UAftermathMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	PrimaryActorTick.bCanEverTick = true;

	GetCapsuleComponent()->InitCapsuleSize(35.f, 90.f);

	// Controller yaw drives the body (shooter-style), pitch/roll only move the camera.
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = true;
	bUseControllerRotationRoll = false;

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(GetCapsuleComponent());
	CameraBoom->bUsePawnControlRotation = true;
	CameraBoom->bDoCollisionTest = true;
	// Smooth the camera through stance changes and vaults.
	CameraBoom->bEnableCameraLag = true;
	CameraBoom->CameraLagSpeed = 18.f;

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;

	Inventory = CreateDefaultSubobject<UAftermathInventoryComponent>(TEXT("Inventory"));
	Survival = CreateDefaultSubobject<UAftermathSurvivalComponent>(TEXT("Survival"));
	Combat = CreateDefaultSubobject<UAftermathCombatComponent>(TEXT("Combat"));

#if !UE_BUILD_SHIPPING
	// Inert unless the process was started with -AftermathScript=<name>.
	TestScript = CreateDefaultSubobject<UAftermathTestScript>(TEXT("TestScript"));
#endif

	PlaceholderBody = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PlaceholderBody"));
	PlaceholderBody->SetupAttachment(GetCapsuleComponent());
	PlaceholderBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (CylinderMesh.Succeeded())
	{
		PlaceholderBody->SetStaticMesh(CylinderMesh.Object);
	}

	UpdatePlaceholderBody();
	ApplyCameraMode();
}

void AAftermathCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	// Inventory and survival replicate themselves (owner only) as components.
	DOREPLIFETIME_CONDITION(AAftermathCharacter, bReplicatedProne, COND_SimulatedOnly);
	DOREPLIFETIME(AAftermathCharacter, bDead);
}

bool AAftermathCharacter::IsDead() const
{
	return bDead;
}

void AAftermathCharacter::SetAimZoom(bool bZoom)
{
	if (FollowCamera)
	{
		FollowCamera->SetFieldOfView(bZoom ? 60.f : 90.f);
	}
	if (CameraBoom && !bFirstPerson)
	{
		CameraBoom->TargetArmLength = bZoom ? ThirdPersonArmLength * 0.5f : ThirdPersonArmLength;
	}
}

float AAftermathCharacter::GetThirdPersonCameraReach() const
{
	// Arm length plus the shoulder offset: the farthest the view point can be from the eye.
	return ThirdPersonArmLength + ThirdPersonShoulderOffset.Size();
}

void AAftermathCharacter::Landed(const FHitResult& Hit)
{
	// Velocity still holds the impact speed when Landed() runs.
	const float ImpactSpeed = -GetCharacterMovement()->Velocity.Z;
	Super::Landed(Hit);

	if (HasAuthority() && Survival)
	{
		Survival->OnLanded(ImpactSpeed);
	}
}

void AAftermathCharacter::HandleDeath(AActor* Victim)
{
	if (!HasAuthority() || bDead)
	{
		return;
	}
	bDead = true;
	// Everything you had ends up on the ground for others to loot (DayZ keeps it on the body; stage 3 adds bodies).
	if (Inventory)
	{
		Inventory->DropAll();
	}
	ApplyDeadState();
	ForceNetUpdate();

	if (AAftermathGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<AAftermathGameMode>() : nullptr)
	{
		GM->OnSurvivorDied(this);
	}
}

void AAftermathCharacter::OnRep_Dead()
{
	ApplyDeadState();
}

void AAftermathCharacter::ApplyDeadState()
{
	if (!bDead)
	{
		return;
	}
	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->StopMovementImmediately();
		Move->DisableMovement();
	}
	// Grey-box corpse: lie the body down.
	if (PlaceholderBody)
	{
		PlaceholderBody->SetRelativeRotation(FRotator(0.f, 0.f, 90.f));
		PlaceholderBody->SetRelativeLocation(FVector(0.f, 0.f, -GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() + 30.f));
	}
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	FocusedActor.Reset();
}

void AAftermathCharacter::RequestUseItem(int32 Uid)
{
	if (HasAuthority())
	{
		AftermathItemActions::Use(*this, Uid);
	}
	else
	{
		ServerUseItem(Uid);
	}
}

void AAftermathCharacter::RequestCombineItems(int32 SourceUid, int32 TargetUid)
{
	if (HasAuthority())
	{
		AftermathItemActions::Combine(*this, SourceUid, TargetUid);
	}
	else
	{
		ServerCombineItems(SourceUid, TargetUid);
	}
}

void AAftermathCharacter::RequestUnloadMagazine(int32 Uid)
{
	if (HasAuthority())
	{
		AftermathItemActions::Unload(*this, Uid);
	}
	else
	{
		ServerUnloadMagazine(Uid);
	}
}

void AAftermathCharacter::ServerUseItem_Implementation(int32 Uid)
{
	if (!bDead)
	{
		AftermathItemActions::Use(*this, Uid);
	}
}

void AAftermathCharacter::ServerCombineItems_Implementation(int32 SourceUid, int32 TargetUid)
{
	if (!bDead)
	{
		AftermathItemActions::Combine(*this, SourceUid, TargetUid);
	}
}

void AAftermathCharacter::ServerUnloadMagazine_Implementation(int32 Uid)
{
	if (!bDead)
	{
		AftermathItemActions::Unload(*this, Uid);
	}
}

void AAftermathCharacter::OnRep_IsCrouched()
{
	// Replace the engine's crouch-only handling so prone and crouch resolve to the right capsule together.
	if (UAftermathMovementComponent* Move = GetAftermathMovement())
	{
		Move->ApplyReplicatedStance(bIsCrouched, bReplicatedProne);
		Move->bNetworkUpdateReceived = true;
	}
}

void AAftermathCharacter::OnRep_ReplicatedProne()
{
	if (UAftermathMovementComponent* Move = GetAftermathMovement())
	{
		Move->ApplyReplicatedStance(bIsCrouched, bReplicatedProne);
	}
}

UAftermathMovementComponent* AAftermathCharacter::GetAftermathMovement() const
{
	return Cast<UAftermathMovementComponent>(GetCharacterMovement());
}

void AAftermathCharacter::BeginPlay()
{
	Super::BeginPlay();
	UpdatePlaceholderBody();
	ApplyCameraMode();

	if (HasAuthority() && Survival)
	{
		Survival->ResetForSpawn();
		Survival->OnDied.AddUObject(this, &AAftermathCharacter::HandleDeath);
	}
}

void AAftermathCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (IsLocallyControlled())
	{
		UpdateFocus();
	}
}

// ---------------------------------------------------------------------------
// Stance
// ---------------------------------------------------------------------------

EAftermathStance AAftermathCharacter::GetStance() const
{
	const UAftermathMovementComponent* Move = GetAftermathMovement();
	return Move ? Move->GetStance() : EAftermathStance::Standing;
}

void AAftermathCharacter::SetSprintInput(bool bPressed)
{
	if (bDead)
	{
		return;
	}
	if (UAftermathMovementComponent* Move = GetAftermathMovement())
	{
		Move->SetWantsSprint(bPressed);
		// Sprinting stands you up, like DayZ.
		if (bPressed)
		{
			Move->SetWantsCrouchStance(false);
			Move->SetWantsProne(false);
		}
	}
}

void AAftermathCharacter::ToggleWalk()
{
	if (UAftermathMovementComponent* Move = GetAftermathMovement())
	{
		Move->SetWantsWalk(!Move->WantsWalk());
	}
}

void AAftermathCharacter::ToggleCrouchStance()
{
	if (UAftermathMovementComponent* Move = GetAftermathMovement())
	{
		Move->SetWantsCrouchStance(!Move->bWantsToCrouch);
	}
}

void AAftermathCharacter::ToggleProneStance()
{
	if (UAftermathMovementComponent* Move = GetAftermathMovement())
	{
		Move->SetWantsProne(!Move->WantsProne());
	}
}

void AAftermathCharacter::JumpOrVault()
{
	UAftermathMovementComponent* Move = GetAftermathMovement();
	if (!Move)
	{
		return;
	}

	// Low stance: space stands you up instead.
	if (Move->WantsProne() || Move->bWantsToCrouch)
	{
		Move->SetWantsProne(false);
		Move->SetWantsCrouchStance(false);
		return;
	}

	if (Move->QueryVault().bCanVault)
	{
		// The flag rides in the next saved move; server re-runs QueryVault before honouring it.
		Move->RequestVault();
	}
	else
	{
		Jump();
	}
}

void AAftermathCharacter::OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
	Super::OnStartCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);
	UpdatePlaceholderBody();
	ApplyCameraMode();
}

void AAftermathCharacter::OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
	Super::OnEndCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);
	UpdatePlaceholderBody();
	ApplyCameraMode();
}

void AAftermathCharacter::UpdatePlaceholderBody()
{
	if (!PlaceholderBody || !GetCapsuleComponent())
	{
		return;
	}
	// BasicShapes cylinder: 100uu tall, 100uu diameter, centred pivot. Fill the capsule's current height.
	const float Half = GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
	const float Radius = GetCapsuleComponent()->GetUnscaledCapsuleRadius();
	PlaceholderBody->SetRelativeScale3D(FVector(Radius * 2.f / 100.f * 0.85f, Radius * 2.f / 100.f * 0.85f, Half * 2.f / 100.f));
	PlaceholderBody->SetRelativeLocation(FVector::ZeroVector);
}

// ---------------------------------------------------------------------------
// Camera
// ---------------------------------------------------------------------------

void AAftermathCharacter::ToggleCameraMode()
{
	bFirstPerson = !bFirstPerson;
	ApplyCameraMode();
}

void AAftermathCharacter::ApplyCameraMode()
{
	if (!CameraBoom || !GetCapsuleComponent())
	{
		return;
	}

	// Eye near the top of whatever capsule we currently have (stance-aware).
	const float Half = GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
	const float EyeZ = Half * 0.78f;

	if (bFirstPerson)
	{
		CameraBoom->TargetArmLength = 0.f;
		CameraBoom->SocketOffset = FVector::ZeroVector;
		CameraBoom->SetRelativeLocation(FVector(FirstPersonEyeForward, 0.f, EyeZ));
		CameraBoom->bDoCollisionTest = false;
	}
	else
	{
		CameraBoom->TargetArmLength = ThirdPersonArmLength;
		CameraBoom->SocketOffset = ThirdPersonShoulderOffset;
		CameraBoom->SetRelativeLocation(FVector(0.f, 0.f, EyeZ - 60.f));
		CameraBoom->bDoCollisionTest = true;
	}

	if (PlaceholderBody)
	{
		PlaceholderBody->SetOwnerNoSee(bFirstPerson);
	}
}

// ---------------------------------------------------------------------------
// Interaction
// ---------------------------------------------------------------------------

FVector AAftermathCharacter::GetInteractEyeLocation() const
{
	const float Half = GetCapsuleComponent() ? GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.f;
	return GetActorLocation() + FVector(0.f, 0.f, Half * 0.78f);
}

IAftermathInteractable* AAftermathCharacter::ResolveInteractable(AActor* Actor) const
{
	return Actor ? Cast<IAftermathInteractable>(Actor) : nullptr;
}

bool AAftermathCharacter::IsWithinInteractReach(const AActor* Target) const
{
	const IAftermathInteractable* Interactable = Target ? Cast<IAftermathInteractable>(Target) : nullptr;
	if (!Interactable)
	{
		return false;
	}
	return AftermathMath::IsWithinInteractRange(GetInteractEyeLocation(), Interactable->GetInteractLocation(),
		InteractRange, InteractServerTolerance);
}

void AAftermathCharacter::UpdateFocus()
{
	FocusedActor.Reset();

	const AController* PC = GetController();
	if (!PC || !GetWorld())
	{
		return;
	}

	// Trace from the camera (what you see), but measure reach from the body (what the server checks).
	FVector ViewLoc;
	FRotator ViewRot;
	PC->GetPlayerViewPoint(ViewLoc, ViewRot);
	const FVector Dir = ViewRot.Vector();
	const float CameraToEye = FVector::Dist(ViewLoc, GetInteractEyeLocation());
	const FVector End = ViewLoc + Dir * (InteractRange + CameraToEye);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(AftermathInteractTrace), false, this);
	FHitResult Hit;
	if (!GetWorld()->LineTraceSingleByChannel(Hit, ViewLoc, End, ECC_Visibility, Params))
	{
		return;
	}

	AActor* HitActor = Hit.GetActor();
	IAftermathInteractable* Interactable = ResolveInteractable(HitActor);
	if (!Interactable || !Interactable->CanInteract(this))
	{
		return;
	}
	// Client uses the strict range (no tolerance) so the prompt never lies about what the server will accept.
	if (!AftermathMath::IsWithinInteractRange(GetInteractEyeLocation(), Interactable->GetInteractLocation(), InteractRange, 0.f))
	{
		return;
	}
	FocusedActor = HitActor;
}

FText AAftermathCharacter::GetFocusedPrompt() const
{
	AActor* Actor = FocusedActor.Get();
	const IAftermathInteractable* Interactable = Actor ? Cast<IAftermathInteractable>(Actor) : nullptr;
	return Interactable ? Interactable->GetInteractPrompt(this) : FText::GetEmpty();
}

void AAftermathCharacter::TryInteract()
{
	AActor* Target = FocusedActor.Get();
	if (!Target)
	{
		return;
	}
	if (HasAuthority())
	{
		ServerTryInteractWith(Target);
	}
	else
	{
		ServerInteract(Target);
	}
}

void AAftermathCharacter::DebugServerInteract(AActor* Target)
{
#if !UE_BUILD_SHIPPING
	if (HasAuthority())
	{
		ServerTryInteractWith(Target);
	}
	else
	{
		ServerInteract(Target);
	}
#endif
}

void AAftermathCharacter::DebugServerFire(const FVector& Muzzle, const FVector& Direction)
{
#if !UE_BUILD_SHIPPING
	if (Combat)
	{
		Combat->DebugSendFire(Muzzle, Direction);
	}
#endif
}

void AAftermathCharacter::DebugGiveLoadedMagazine(FName MagId, int32 Rounds)
{
#if !UE_BUILD_SHIPPING
	ServerDebugGiveMagazine(MagId, Rounds);
#endif
}

void AAftermathCharacter::DebugGiveItem(FName ItemId)
{
#if !UE_BUILD_SHIPPING
	ServerDebugGiveItem(ItemId);
#endif
}

void AAftermathCharacter::ServerDebugGiveItem_Implementation(FName ItemId)
{
#if !UE_BUILD_SHIPPING
	if (!FParse::Param(FCommandLine::Get(), TEXT("AftermathAllowDebug")))
	{
		UE_LOG(LogAftermath, Warning, TEXT("[Aftermath] DebugRejected op=give_item by=%s (server not started with -AftermathAllowDebug)"), *GetName());
		return;
	}
	const FAftermathItemDef* Def = UAftermathItemDatabase::FindDef(ItemId);
	if (!Def || !Inventory)
	{
		return;
	}
	Inventory->AddItem(AftermathItems::MakeInstance(*Def));
	UE_LOG(LogAftermath, Display, TEXT("[Aftermath] DebugGive item=%s to=%s"), *ItemId.ToString(), *GetName());
#endif
}

void AAftermathCharacter::ServerDebugGiveMagazine_Implementation(FName MagId, int32 Rounds)
{
#if !UE_BUILD_SHIPPING
	// Only on servers explicitly started for testing.
	if (!FParse::Param(FCommandLine::Get(), TEXT("AftermathAllowDebug")))
	{
		UE_LOG(LogAftermath, Warning, TEXT("[Aftermath] DebugRejected op=give_magazine by=%s (server not started with -AftermathAllowDebug)"), *GetName());
		return;
	}
	const UAftermathItemDatabase* DB = UAftermathItemDatabase::Get();
	const FAftermathItemDef* Def = DB ? DB->Find(MagId) : nullptr;
	if (!Def || !Def->IsMagazine() || !Inventory)
	{
		return;
	}
	FAftermathItemInstance Mag = AftermathItems::MakeInstance(*Def);
	Mag.Loaded = FMath::Clamp(Rounds, 0, Def->Magazine.Capacity);
	Inventory->AddItem(Mag);
	UE_LOG(LogAftermath, Display, TEXT("[Aftermath] DebugGive item=%s rounds=%d to=%s"), *MagId.ToString(), Mag.Loaded, *GetName());
#endif
}

void AAftermathCharacter::ServerInteract_Implementation(AActor* Target)
{
	ServerTryInteractWith(Target);
}

bool AAftermathCharacter::ServerTryInteractWith(AActor* Target)
{
	if (!HasAuthority() || bDead)
	{
		return false;
	}

	IAftermathInteractable* Interactable = ResolveInteractable(Target);
	if (!Interactable)
	{
		return false;
	}
	if (!Interactable->CanInteract(this))
	{
		return false;
	}
	if (!IsWithinInteractReach(Target))
	{
		// Never trust the client's claim that it was close enough.
		UE_LOG(LogAftermath, Warning, TEXT("[Aftermath] InteractRejected reason=range target=%s by=%s dist=%.0f"),
			*Target->GetName(), *GetName(), FVector::Dist(GetInteractEyeLocation(), Interactable->GetInteractLocation()));
		return false;
	}

	Interactable->Interact(this);
	return true;
}

#undef LOCTEXT_NAMESPACE
