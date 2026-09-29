#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "UI/AftermathInventoryLayout.h"
#include "AftermathPlayerController.generated.h"

class UInputAction;
class UInputMappingContext;
class AAftermathCharacter;
class AAftermathPickup;
struct FInputActionValue;

/** What the mouse is currently dragging in the inventory panel. */
struct FAftermathDragState
{
	bool bActive = false;
	/** Carried or worn item uid (0 when dragging a ground item). */
	int32 Uid = 0;
	/** Worn slot when dragging a worn item. */
	EAftermathWearSlot FromSlot = EAftermathWearSlot::None;
	bool bFromWorn = false;
	TWeakObjectPtr<AAftermathPickup> FromGround;
	FName Def;
	bool bRotated = false;
	/** Cell inside the item that was grabbed, so the item keeps its offset under the cursor. */
	int32 GrabCellX = 0;
	int32 GrabCellY = 0;
};

/**
 * Local input handling. Input actions and the mapping context are built in code so the
 * grey-box stages need no binary input assets; they can be swapped for .uasset versions later.
 *
 * Keys: WASD move, mouse look, Shift sprint (hold), Ctrl walk (toggle), C crouch, Z prone,
 *       Space jump / vault, F interact, V camera, Tab inventory, 1-9 hotbar,
 *       LMB fire / melee, RMB aim (hold), R reload.
 * Inventory panel: drag with LMB (move / wear / take off / combine / drop), R rotate while dragging,
 *       RMB use, G drop, X split a stack in half, 1-9 while hovering assigns the hotbar.
 *
 * The controller only ever SENDS requests; the server validates and the result arrives via replication.
 */
UCLASS()
class AFTERMATH_API AAftermathPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	// ---- UI state read by the HUD ----
	bool IsInventoryOpen() const { return bInventoryOpen; }
	const FAftermathDragState& GetDrag() const { return Drag; }
	const TArray<int32>& GetHotbar() const { return Hotbar; }
	const FAftermathInventoryLayout& GetLayout() const { return Layout; }
	const TArray<AAftermathPickup*>& GetNearbyPickups() const { return NearbyPickups; }

	/** HUD calls this every frame with the viewport size before drawing; clicks are hit-tested against it. */
	void UpdateLayout(float ViewW, float ViewH);

	/** Current mouse position in viewport pixels (0,0 if none). */
	FVector2D GetMouse() const;

	/** Where the dragged item would land right now, for the HUD ghost. Returns false when not over a grid. */
	bool GetDragPreview(EAftermathWearSlot& OutContainer, int32& OutX, int32& OutY, bool& bOutValid) const;

	/** Short-lived message line (e.g. "需要开罐工具"). */
	const FString& GetToast() const { return Toast; }
	float GetToastTimeLeft() const { return ToastTime; }
	void ShowToast(const FString& Message, float Seconds = 2.5f);

	// ---- Dev/test hooks (used by the scripted network test driver) ----
	void SetInventoryOpen(bool bOpen);

protected:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;
	virtual void PlayerTick(float DeltaTime) override;

private:
	void BuildDefaultInput();
	UInputAction* MakeAction(const TCHAR* Name, bool bAxis2D);
	AAftermathCharacter* GetSurvivor() const;
	void RefreshNearby();

	void HandleMove(const FInputActionValue& Value);
	void HandleLook(const FInputActionValue& Value);
	void HandleJump();
	void HandleStopJump();
	void HandleToggleCamera();
	void HandleSprintPressed();
	void HandleSprintReleased();
	void HandleWalk();
	void HandleCrouch();
	void HandleProne();
	void HandleInteract();
	void HandleToggleInventory();
	void HandleClickPressed();
	void HandleClickReleased();
	void HandleUse();
	void HandleRotate();
	void HandleAimPressed();
	void HandleAimReleased();
	void HandleDrop();
	void HandleSplit();
	void HandleHotbar(int32 Index);

	void FinishDrag(const FAftermathInvHit& Target);

	UPROPERTY(Transient) TObjectPtr<UInputMappingContext> DefaultContext;
	UPROPERTY(Transient) TObjectPtr<UInputAction> MoveAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> LookAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> JumpAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> ToggleCameraAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> SprintAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> WalkAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> CrouchAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> ProneAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> InteractAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> InventoryAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> ClickAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> UseAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> RotateAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> DropAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> SplitAction;
	UPROPERTY(Transient) TArray<TObjectPtr<UInputAction>> HotbarActions;

	bool bInventoryOpen = false;
	FAftermathDragState Drag;
	FAftermathInventoryLayout Layout;
	TArray<AAftermathPickup*> NearbyPickups;
	float NearbyTimer = 0.f;
	/** Item uid per hotbar slot. Client-side preference; stale uids are simply skipped. */
	TArray<int32> Hotbar;
	FString Toast;
	float ToastTime = 0.f;
};
