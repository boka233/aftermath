#include "Interaction/AftermathDoor.h"

#include "Aftermath.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Net/UnrealNetwork.h"
#include "Player/AftermathCharacter.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "AftermathDoor"

AAftermathDoor::AAftermathDoor()
{
	bReplicates = true;
	SetNetUpdateFrequency(10.f);
	// Doors rarely change; wake up only when toggled.
	NetDormancy = DORM_DormantAll;

	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	Hinge = CreateDefaultSubobject<USceneComponent>(TEXT("Hinge"));
	SetRootComponent(Hinge);

	Leaf = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Leaf"));
	Leaf->SetupAttachment(Hinge);
	Leaf->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);
	Leaf->SetMobility(EComponentMobility::Movable);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded())
	{
		Leaf->SetStaticMesh(Cube.Object);
	}

	ApplyLeafLayout();
}

void AAftermathDoor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyLeafLayout();
	ApplyYaw(CurrentYaw);
}

void AAftermathDoor::ApplyLeafLayout()
{
	// Cube is 100uu with a centred pivot. Hinge sits at the door's left edge, floor level.
	Leaf->SetRelativeScale3D(DoorSize / 100.f);
	Leaf->SetRelativeLocation(FVector(DoorSize.X * 0.5f, 0.f, DoorSize.Z * 0.5f));
}

void AAftermathDoor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AAftermathDoor, OpenState);
}

FText AAftermathDoor::GetInteractPrompt(const AAftermathCharacter* User) const
{
	return IsOpen() ? LOCTEXT("Close", "关门") : LOCTEXT("Open", "开门");
}

FVector AAftermathDoor::GetInteractLocation() const
{
	// Middle of the leaf in its closed position (stable regardless of swing state).
	return GetActorTransform().TransformPosition(FVector(DoorSize.X * 0.5f, 0.f, DoorSize.Z * 0.5f));
}

void AAftermathDoor::Interact(AAftermathCharacter* User)
{
	const FVector From = User ? User->GetActorLocation() : GetActorLocation();
	SetOpen(!IsOpen(), From);
	UE_LOG(LogAftermath, Display, TEXT("[Aftermath] DoorToggled door=%s open=%d by=%s"),
		*GetName(), IsOpen() ? 1 : 0, User ? *User->GetName() : TEXT("?"));
}

void AAftermathDoor::SetOpen(bool bNewOpen, const FVector& FromLocation)
{
	if (!HasAuthority() || IsOpen() == bNewOpen)
	{
		return;
	}

	int8 NewState = 0;
	if (bNewOpen)
	{
		// Leaf lies along local +X with its faces toward local ±Y. Positive yaw swings it toward +Y,
		// so pick the side opposite the user.
		const float UserLocalY = GetActorTransform().InverseTransformPosition(FromLocation).Y;
		NewState = UserLocalY < 0.f ? 1 : -1;
	}

	FlushNetDormancy();
	OpenState = NewState;
	OnRep_Open(); // server runs the same visual path
}

void AAftermathDoor::OnRep_Open()
{
	SetActorTickEnabled(true);
}

void AAftermathDoor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const float Target = OpenYaw * OpenState;
	// Signed linear step (FixedTurn wraps to 0..360, which breaks negative swing angles).
	const float NewYaw = FMath::FInterpConstantTo(CurrentYaw, Target, DeltaSeconds, SwingSpeedDegPerSec);
	ApplyYaw(NewYaw);

	if (FMath::IsNearlyEqual(NewYaw, Target, 0.01f))
	{
		SetActorTickEnabled(false);
	}
}

void AAftermathDoor::ApplyYaw(float Yaw)
{
	CurrentYaw = Yaw;
	// Rotate the leaf around the hinge (the leaf's parent), not the actor, so the actor transform stays static.
	Leaf->SetRelativeLocation(FRotator(0.f, Yaw, 0.f).RotateVector(FVector(DoorSize.X * 0.5f, 0.f, DoorSize.Z * 0.5f)));
	Leaf->SetRelativeRotation(FRotator(0.f, Yaw, 0.f));
}

#undef LOCTEXT_NAMESPACE
