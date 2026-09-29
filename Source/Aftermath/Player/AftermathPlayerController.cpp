#include "Player/AftermathPlayerController.h"

#include "EngineUtils.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Combat/AftermathCombatComponent.h"
#include "Engine/LocalPlayer.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Interaction/AftermathPickup.h"
#include "Items/AftermathGrid.h"
#include "Items/AftermathInventoryComponent.h"
#include "Items/AftermathItemDatabase.h"
#include "Player/AftermathCharacter.h"

#define LOCTEXT_NAMESPACE "AftermathPC"

namespace
{
	/** Map a key to an action with optional modifiers (used to turn WASD into one 2D axis). */
	void MapKey(UInputMappingContext* Context, UInputAction* Action, const FKey& Key,
		bool bNegate = false, bool bSwizzleYX = false)
	{
		FEnhancedActionKeyMapping& Mapping = Context->MapKey(Action, Key);

		if (bSwizzleYX)
		{
			UInputModifierSwizzleAxis* Swizzle = NewObject<UInputModifierSwizzleAxis>(Context);
			Swizzle->Order = EInputAxisSwizzle::YXZ;
			Mapping.Modifiers.Add(Swizzle);
		}
		if (bNegate)
		{
			Mapping.Modifiers.Add(NewObject<UInputModifierNegate>(Context));
		}
	}

	/** Ground items listed in the "nearby" column. Slightly less than the server's pickup reach. */
	constexpr float NearbyRadius = 250.f;
}

AAftermathCharacter* AAftermathPlayerController::GetSurvivor() const
{
	return Cast<AAftermathCharacter>(GetPawn());
}

void AAftermathPlayerController::BeginPlay()
{
	Super::BeginPlay();

	Hotbar.Init(0, FAftermathInventoryLayout::HotbarSize);

	if (!IsLocalController())
	{
		return;
	}

	if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
		ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
	{
		Subsystem->AddMappingContext(DefaultContext, 0);
	}

	SetInventoryOpen(false);
}

UInputAction* AAftermathPlayerController::MakeAction(const TCHAR* Name, bool bAxis2D)
{
	UInputAction* Action = NewObject<UInputAction>(this, Name);
	Action->ValueType = bAxis2D ? EInputActionValueType::Axis2D : EInputActionValueType::Boolean;
	return Action;
}

void AAftermathPlayerController::BuildDefaultInput()
{
	if (DefaultContext)
	{
		return;
	}

	MoveAction = MakeAction(TEXT("IA_Move"), true);
	LookAction = MakeAction(TEXT("IA_Look"), true);
	JumpAction = MakeAction(TEXT("IA_Jump"), false);
	ToggleCameraAction = MakeAction(TEXT("IA_ToggleCamera"), false);
	SprintAction = MakeAction(TEXT("IA_Sprint"), false);
	WalkAction = MakeAction(TEXT("IA_Walk"), false);
	CrouchAction = MakeAction(TEXT("IA_Crouch"), false);
	ProneAction = MakeAction(TEXT("IA_Prone"), false);
	InteractAction = MakeAction(TEXT("IA_Interact"), false);
	InventoryAction = MakeAction(TEXT("IA_Inventory"), false);
	ClickAction = MakeAction(TEXT("IA_Click"), false);
	UseAction = MakeAction(TEXT("IA_Use"), false);
	RotateAction = MakeAction(TEXT("IA_Rotate"), false);
	DropAction = MakeAction(TEXT("IA_Drop"), false);
	SplitAction = MakeAction(TEXT("IA_Split"), false);

	DefaultContext = NewObject<UInputMappingContext>(this, TEXT("IMC_Default"));

	// Move: X = right, Y = forward.
	MapKey(DefaultContext, MoveAction, EKeys::W, false, true);
	MapKey(DefaultContext, MoveAction, EKeys::S, true, true);
	MapKey(DefaultContext, MoveAction, EKeys::D);
	MapKey(DefaultContext, MoveAction, EKeys::A, true);

	// Look: mouse delta, Y inverted so moving the mouse up looks up.
	FEnhancedActionKeyMapping& LookMap = DefaultContext->MapKey(LookAction, EKeys::Mouse2D);
	UInputModifierNegate* InvertY = NewObject<UInputModifierNegate>(DefaultContext);
	InvertY->bX = false;
	InvertY->bY = true;
	InvertY->bZ = false;
	LookMap.Modifiers.Add(InvertY);

	DefaultContext->MapKey(JumpAction, EKeys::SpaceBar);
	DefaultContext->MapKey(ToggleCameraAction, EKeys::V);
	DefaultContext->MapKey(SprintAction, EKeys::LeftShift);
	DefaultContext->MapKey(WalkAction, EKeys::LeftControl);
	DefaultContext->MapKey(CrouchAction, EKeys::C);
	DefaultContext->MapKey(ProneAction, EKeys::Z);
	DefaultContext->MapKey(InteractAction, EKeys::F);
	DefaultContext->MapKey(InventoryAction, EKeys::Tab);
	DefaultContext->MapKey(ClickAction, EKeys::LeftMouseButton);
	DefaultContext->MapKey(UseAction, EKeys::RightMouseButton);
	DefaultContext->MapKey(RotateAction, EKeys::R);
	DefaultContext->MapKey(DropAction, EKeys::G);
	DefaultContext->MapKey(SplitAction, EKeys::X);

	const FKey NumberKeys[] = { EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five,
		EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine };
	for (int32 i = 0; i < UE_ARRAY_COUNT(NumberKeys); ++i)
	{
		UInputAction* A = MakeAction(*FString::Printf(TEXT("IA_Hotbar%d"), i + 1), false);
		HotbarActions.Add(A);
		DefaultContext->MapKey(A, NumberKeys[i]);
	}
}

void AAftermathPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	BuildDefaultInput();

	UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(InputComponent);
	if (!Input)
	{
		return;
	}

	Input->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AAftermathPlayerController::HandleMove);
	Input->BindAction(LookAction, ETriggerEvent::Triggered, this, &AAftermathPlayerController::HandleLook);
	Input->BindAction(JumpAction, ETriggerEvent::Started, this, &AAftermathPlayerController::HandleJump);
	Input->BindAction(JumpAction, ETriggerEvent::Completed, this, &AAftermathPlayerController::HandleStopJump);
	Input->BindAction(ToggleCameraAction, ETriggerEvent::Started, this, &AAftermathPlayerController::HandleToggleCamera);
	Input->BindAction(SprintAction, ETriggerEvent::Started, this, &AAftermathPlayerController::HandleSprintPressed);
	Input->BindAction(SprintAction, ETriggerEvent::Completed, this, &AAftermathPlayerController::HandleSprintReleased);
	Input->BindAction(WalkAction, ETriggerEvent::Started, this, &AAftermathPlayerController::HandleWalk);
	Input->BindAction(CrouchAction, ETriggerEvent::Started, this, &AAftermathPlayerController::HandleCrouch);
	Input->BindAction(ProneAction, ETriggerEvent::Started, this, &AAftermathPlayerController::HandleProne);
	Input->BindAction(InteractAction, ETriggerEvent::Started, this, &AAftermathPlayerController::HandleInteract);
	Input->BindAction(InventoryAction, ETriggerEvent::Started, this, &AAftermathPlayerController::HandleToggleInventory);
	Input->BindAction(ClickAction, ETriggerEvent::Started, this, &AAftermathPlayerController::HandleClickPressed);
	Input->BindAction(ClickAction, ETriggerEvent::Completed, this, &AAftermathPlayerController::HandleClickReleased);
	Input->BindAction(UseAction, ETriggerEvent::Started, this, &AAftermathPlayerController::HandleUse);
	// Right mouse outside the panel = aim down sights (hold).
	Input->BindAction(UseAction, ETriggerEvent::Started, this, &AAftermathPlayerController::HandleAimPressed);
	Input->BindAction(UseAction, ETriggerEvent::Completed, this, &AAftermathPlayerController::HandleAimReleased);
	Input->BindAction(RotateAction, ETriggerEvent::Started, this, &AAftermathPlayerController::HandleRotate);
	Input->BindAction(DropAction, ETriggerEvent::Started, this, &AAftermathPlayerController::HandleDrop);
	Input->BindAction(SplitAction, ETriggerEvent::Started, this, &AAftermathPlayerController::HandleSplit);
	for (int32 i = 0; i < HotbarActions.Num(); ++i)
	{
		Input->BindActionValueLambda(HotbarActions[i], ETriggerEvent::Started, [this, i](const FInputActionValue&) { HandleHotbar(i); });
	}
}

void AAftermathPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	if (ToastTime > 0.f)
	{
		ToastTime -= DeltaTime;
	}
	NearbyTimer -= DeltaTime;
	if (bInventoryOpen && NearbyTimer <= 0.f)
	{
		NearbyTimer = 0.25f;
		RefreshNearby();
	}
}

void AAftermathPlayerController::ShowToast(const FString& Message, float Seconds)
{
	Toast = Message;
	ToastTime = Seconds;
}

void AAftermathPlayerController::RefreshNearby()
{
	NearbyPickups.Reset();
	const APawn* P = GetPawn();
	if (!P || !GetWorld())
	{
		return;
	}
	const FVector Me = P->GetActorLocation();
	for (TActorIterator<AAftermathPickup> It(GetWorld()); It; ++It)
	{
		if (!It->IsActorBeingDestroyed() && It->GetItem().IsValid() && FVector::Dist(It->GetActorLocation(), Me) <= NearbyRadius)
		{
			NearbyPickups.Add(*It);
		}
	}
	NearbyPickups.Sort([&Me](const AAftermathPickup& A, const AAftermathPickup& B)
	{
		return FVector::DistSquared(A.GetActorLocation(), Me) < FVector::DistSquared(B.GetActorLocation(), Me);
	});
}

void AAftermathPlayerController::UpdateLayout(float ViewW, float ViewH)
{
	const AAftermathCharacter* C = GetSurvivor();
	if (const UAftermathInventoryComponent* Inv = C ? C->GetInventory() : nullptr)
	{
		Layout.Build(ViewW, ViewH, *Inv, NearbyPickups, bInventoryOpen);
	}
}

FVector2D AAftermathPlayerController::GetMouse() const
{
	float X = 0.f, Y = 0.f;
	if (!GetMousePosition(X, Y))
	{
		return FVector2D::ZeroVector;
	}
	return FVector2D(X, Y);
}

void AAftermathPlayerController::SetInventoryOpen(bool bOpen)
{
	bInventoryOpen = bOpen;
	Drag = FAftermathDragState();
	// Never keep shooting or aiming with the panel open.
	if (bOpen)
	{
		HandleAimReleased();
		if (AAftermathCharacter* Char = GetSurvivor())
		{
			if (UAftermathCombatComponent* Combat = Char->GetCombat())
			{
				Combat->SetTriggerHeld(false);
			}
		}
	}
	if (bOpen)
	{
		RefreshNearby();
		FInputModeGameAndUI Mode;
		Mode.SetHideCursorDuringCapture(false);
		SetInputMode(Mode);
		SetShowMouseCursor(true);
		// Don't let the mouse keep turning the camera while you sort your stuff.
		SetIgnoreLookInput(true);
	}
	else
	{
		SetInputMode(FInputModeGameOnly());
		SetShowMouseCursor(false);
		ResetIgnoreLookInput();
	}
}

// ---------------------------------------------------------------------------
// Movement input
// ---------------------------------------------------------------------------

void AAftermathPlayerController::HandleMove(const FInputActionValue& Value)
{
	APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn)
	{
		return;
	}

	const FVector2D Axis = Value.Get<FVector2D>();
	const FRotator YawOnly(0.f, GetControlRotation().Yaw, 0.f);
	const FRotationMatrix Basis(YawOnly);

	ControlledPawn->AddMovementInput(Basis.GetUnitAxis(EAxis::X), Axis.Y);
	ControlledPawn->AddMovementInput(Basis.GetUnitAxis(EAxis::Y), Axis.X);
}

void AAftermathPlayerController::HandleLook(const FInputActionValue& Value)
{
	const FVector2D Axis = Value.Get<FVector2D>();
	AddYawInput(Axis.X);
	AddPitchInput(Axis.Y);
}

void AAftermathPlayerController::HandleJump()
{
	if (AAftermathCharacter* Char = GetSurvivor())
	{
		Char->JumpOrVault();
	}
}

void AAftermathPlayerController::HandleStopJump()
{
	if (ACharacter* Char = GetPawn<ACharacter>())
	{
		Char->StopJumping();
	}
}

void AAftermathPlayerController::HandleToggleCamera()
{
	if (AAftermathCharacter* Char = GetSurvivor())
	{
		Char->ToggleCameraMode();
	}
}

void AAftermathPlayerController::HandleSprintPressed()
{
	if (AAftermathCharacter* Char = GetSurvivor())
	{
		Char->SetSprintInput(true);
	}
}

void AAftermathPlayerController::HandleSprintReleased()
{
	if (AAftermathCharacter* Char = GetSurvivor())
	{
		Char->SetSprintInput(false);
	}
}

void AAftermathPlayerController::HandleWalk()
{
	if (AAftermathCharacter* Char = GetSurvivor())
	{
		Char->ToggleWalk();
	}
}

void AAftermathPlayerController::HandleCrouch()
{
	if (AAftermathCharacter* Char = GetSurvivor())
	{
		Char->ToggleCrouchStance();
	}
}

void AAftermathPlayerController::HandleProne()
{
	if (AAftermathCharacter* Char = GetSurvivor())
	{
		Char->ToggleProneStance();
	}
}

void AAftermathPlayerController::HandleInteract()
{
	if (bInventoryOpen)
	{
		return;
	}
	if (AAftermathCharacter* Char = GetSurvivor())
	{
		Char->TryInteract();
	}
}

void AAftermathPlayerController::HandleToggleInventory()
{
	SetInventoryOpen(!bInventoryOpen);
}

// ---------------------------------------------------------------------------
// Inventory panel
// ---------------------------------------------------------------------------

void AAftermathPlayerController::HandleClickPressed()
{
	// Inventory closed: left mouse is the trigger (fire, or melee with a melee weapon / fists).
	if (!bInventoryOpen)
	{
		if (AAftermathCharacter* Char = GetSurvivor())
		{
			if (UAftermathCombatComponent* Combat = Char->GetCombat())
			{
				Combat->SetTriggerHeld(true);
			}
		}
		return;
	}

	AAftermathCharacter* C = GetSurvivor();
	UAftermathInventoryComponent* Inv = C ? C->GetInventory() : nullptr;
	if (!Inv)
	{
		return;
	}
	const FVector2D M = GetMouse();
	const FAftermathInvHit Hit = Layout.HitTest(M.X, M.Y, *Inv, Hotbar);

	Drag = FAftermathDragState();
	switch (Hit.Kind)
	{
	case FAftermathInvHit::EKind::GridCell:
		if (const FAftermathGrid* G = Inv->GetGrid(Hit.Container))
		{
			if (const FAftermathGridEntry* E = G->FindEntry(Hit.Uid))
			{
				Drag.bActive = true;
				Drag.Uid = E->Item.Uid;
				Drag.Def = E->Item.Def;
				Drag.bRotated = E->bRotated;
				Drag.GrabCellX = Hit.CellX - E->X;
				Drag.GrabCellY = Hit.CellY - E->Y;
			}
		}
		break;

	case FAftermathInvHit::EKind::EquipSlot:
		if (Hit.Uid != 0)
		{
			Drag.bActive = true;
			Drag.Uid = Hit.Uid;
			Drag.bFromWorn = true;
			Drag.FromSlot = Hit.Container;
			if (const FAftermathItemInstance* I = Inv->FindItem(Hit.Uid)) { Drag.Def = I->Def; }
		}
		break;

	case FAftermathInvHit::EKind::Nearby:
		if (AAftermathPickup* P = Hit.Pickup.Get())
		{
			Drag.bActive = true;
			Drag.FromGround = P;
			Drag.Def = P->GetItemId();
		}
		break;

	default:
		break;
	}
}

void AAftermathPlayerController::HandleClickReleased()
{
	// Trigger release always goes through (also if the panel was opened while holding fire).
	if (AAftermathCharacter* Char = GetSurvivor())
	{
		if (UAftermathCombatComponent* Combat = Char->GetCombat())
		{
			Combat->SetTriggerHeld(false);
		}
	}

	AAftermathCharacter* C = GetSurvivor();
	UAftermathInventoryComponent* Inv = C ? C->GetInventory() : nullptr;
	if (!bInventoryOpen || !Inv || !Drag.bActive)
	{
		Drag = FAftermathDragState();
		return;
	}
	const FVector2D M = GetMouse();
	FinishDrag(Layout.HitTest(M.X, M.Y, *Inv, Hotbar));
	Drag = FAftermathDragState();
}

bool AAftermathPlayerController::GetDragPreview(EAftermathWearSlot& OutContainer, int32& OutX, int32& OutY, bool& bOutValid) const
{
	const AAftermathCharacter* C = GetSurvivor();
	const UAftermathInventoryComponent* Inv = C ? C->GetInventory() : nullptr;
	if (!Drag.bActive || !Inv)
	{
		return false;
	}
	const FVector2D M = GetMouse();
	const FAftermathInvHit Hit = Layout.HitTest(M.X, M.Y, *Inv, Hotbar);
	if (Hit.Kind != FAftermathInvHit::EKind::GridCell)
	{
		return false;
	}
	OutContainer = Hit.Container;
	OutX = Hit.CellX - (Drag.bRotated ? Drag.GrabCellY : Drag.GrabCellX);
	OutY = Hit.CellY - (Drag.bRotated ? Drag.GrabCellX : Drag.GrabCellY);

	FAftermathItemInstance Probe;
	Probe.Uid = Drag.Uid;
	Probe.Def = Drag.Def;
	const FAftermathGrid* G = Inv->GetGrid(Hit.Container);
	bOutValid = G && G->CanPlace(Probe, OutX, OutY, Drag.bRotated, Drag.Uid);
	return true;
}

void AAftermathPlayerController::FinishDrag(const FAftermathInvHit& Target)
{
	AAftermathCharacter* C = GetSurvivor();
	UAftermathInventoryComponent* Inv = C ? C->GetInventory() : nullptr;
	if (!C || !Inv)
	{
		return;
	}
	const FAftermathItemDef* Def = DefOf(Drag.Def);

	switch (Target.Kind)
	{
	case FAftermathInvHit::EKind::GridCell:
	{
		// Dropped onto another item: combine / merge (knife on can, ammo on mag, stack on stack).
		if (Target.Uid != 0 && Target.Uid != Drag.Uid && Drag.Uid != 0)
		{
			C->RequestCombineItems(Drag.Uid, Target.Uid);
			return;
		}
		EAftermathWearSlot Container;
		int32 X, Y;
		bool bValid;
		if (!GetDragPreview(Container, X, Y, bValid) || !bValid)
		{
			ShowToast(LOCTEXT("NoRoom", "放不下").ToString());
			return;
		}
		if (AAftermathPickup* Ground = Drag.FromGround.Get())
		{
			Inv->ServerTakeFromGround(Ground, EAftermathTakeMode::Place, Container, X, Y, Drag.bRotated);
		}
		else if (Drag.bFromWorn)
		{
			Inv->ServerUnequip(Drag.FromSlot, Container, X, Y, Drag.bRotated);
		}
		else
		{
			Inv->ServerMoveItem(Drag.Uid, Container, X, Y, Drag.bRotated);
		}
		return;
	}

	case FAftermathInvHit::EKind::EquipSlot:
		if (!Def || !Def->IsWearable() || Def->Wear.Slot != Target.Container)
		{
			ShowToast(LOCTEXT("WrongSlot", "穿不到这里").ToString());
			return;
		}
		if (Target.Uid != 0 && !Drag.bFromWorn)
		{
			ShowToast(LOCTEXT("SlotTaken", "先脱下身上那件").ToString());
			return;
		}
		if (AAftermathPickup* Ground = Drag.FromGround.Get())
		{
			Inv->ServerTakeFromGround(Ground, EAftermathTakeMode::Equip, EAftermathWearSlot::None, 0, 0, false);
		}
		else if (!Drag.bFromWorn)
		{
			Inv->ServerEquip(Drag.Uid);
		}
		return;

	case FAftermathInvHit::EKind::Hotbar:
		if (Drag.Uid != 0 && Target.HotbarIndex >= 0)
		{
			Hotbar.Remove(Drag.Uid);
			Hotbar.SetNum(FAftermathInventoryLayout::HotbarSize);
			Hotbar[Target.HotbarIndex] = Drag.Uid;
		}
		return;

	case FAftermathInvHit::EKind::Nearby:
	case FAftermathInvHit::EKind::None:
		// Dragged out of the panel (or onto the ground column): drop it.
		if (Drag.Uid != 0 && !Drag.FromGround.IsValid() && !Layout.Panel.Contains(GetMouse().X, GetMouse().Y))
		{
			Inv->ServerDrop(Drag.Uid);
		}
		return;
	}
}

void AAftermathPlayerController::HandleRotate()
{
	if (bInventoryOpen)
	{
		if (Drag.bActive)
		{
			Drag.bRotated = !Drag.bRotated;
		}
		return;
	}
	// Panel closed: R reloads the gun in hands.
	if (AAftermathCharacter* Char = GetSurvivor())
	{
		if (UAftermathCombatComponent* Combat = Char->GetCombat())
		{
			Combat->RequestReload();
		}
	}
}

void AAftermathPlayerController::HandleAimPressed()
{
	if (bInventoryOpen)
	{
		return;
	}
	if (AAftermathCharacter* Char = GetSurvivor())
	{
		if (UAftermathCombatComponent* Combat = Char->GetCombat())
		{
			Combat->SetAiming(true);
			Char->SetAimZoom(true);
		}
	}
}

void AAftermathPlayerController::HandleAimReleased()
{
	if (AAftermathCharacter* Char = GetSurvivor())
	{
		if (UAftermathCombatComponent* Combat = Char->GetCombat())
		{
			Combat->SetAiming(false);
			Char->SetAimZoom(false);
		}
	}
}

void AAftermathPlayerController::HandleUse()
{
	AAftermathCharacter* C = GetSurvivor();
	UAftermathInventoryComponent* Inv = C ? C->GetInventory() : nullptr;
	if (!bInventoryOpen || !Inv)
	{
		return;
	}
	const FVector2D M = GetMouse();
	const FAftermathInvHit Hit = Layout.HitTest(M.X, M.Y, *Inv, Hotbar);
	if (Hit.Kind == FAftermathInvHit::EKind::Nearby)
	{
		if (AAftermathPickup* P = Hit.Pickup.Get())
		{
			Inv->ServerTakeFromGround(P, EAftermathTakeMode::Auto, EAftermathWearSlot::None, 0, 0, false);
		}
		return;
	}
	if (Hit.Uid != 0)
	{
		const FAftermathItemInstance* Item = Inv->FindItem(Hit.Uid);
		const FAftermathItemDef* Def = Item ? DefOf(Item->Def) : nullptr;
		// Right-click a loaded magazine unloads it; anything else is "use".
		if (Def && Def->IsMagazine() && Item->Loaded > 0)
		{
			C->RequestUnloadMagazine(Hit.Uid);
		}
		else
		{
			C->RequestUseItem(Hit.Uid);
		}
	}
}

void AAftermathPlayerController::HandleDrop()
{
	AAftermathCharacter* C = GetSurvivor();
	UAftermathInventoryComponent* Inv = C ? C->GetInventory() : nullptr;
	if (!bInventoryOpen || !Inv)
	{
		return;
	}
	const FVector2D M = GetMouse();
	const FAftermathInvHit Hit = Layout.HitTest(M.X, M.Y, *Inv, Hotbar);
	if (Hit.Uid != 0)
	{
		Inv->ServerDrop(Hit.Uid);
	}
}

void AAftermathPlayerController::HandleSplit()
{
	AAftermathCharacter* C = GetSurvivor();
	UAftermathInventoryComponent* Inv = C ? C->GetInventory() : nullptr;
	if (!bInventoryOpen || !Inv)
	{
		return;
	}
	const FVector2D M = GetMouse();
	const FAftermathInvHit Hit = Layout.HitTest(M.X, M.Y, *Inv, Hotbar);
	const FAftermathItemInstance* Item = Hit.Uid ? Inv->FindItem(Hit.Uid) : nullptr;
	if (Item && Item->Count > 1)
	{
		Inv->ServerSplit(Hit.Uid, Item->Count / 2);
	}
}

void AAftermathPlayerController::HandleHotbar(int32 Index)
{
	AAftermathCharacter* C = GetSurvivor();
	UAftermathInventoryComponent* Inv = C ? C->GetInventory() : nullptr;
	if (!Inv || !Hotbar.IsValidIndex(Index))
	{
		return;
	}

	// With the panel open, hovering an item and pressing a number assigns it.
	if (bInventoryOpen)
	{
		const FVector2D M = GetMouse();
		const FAftermathInvHit Hit = Layout.HitTest(M.X, M.Y, *Inv, Hotbar);
		if (Hit.Uid != 0 && Hit.Kind != FAftermathInvHit::EKind::Hotbar)
		{
			Hotbar.Remove(Hit.Uid);
			Hotbar.SetNum(FAftermathInventoryLayout::HotbarSize);
			Hotbar[Index] = Hit.Uid;
			return;
		}
	}

	const int32 Uid = Hotbar[Index];
	if (Uid == 0 || !Inv->FindItem(Uid))
	{
		Hotbar[Index] = 0;
		return;
	}
	C->RequestUseItem(Uid);
}

#undef LOCTEXT_NAMESPACE
