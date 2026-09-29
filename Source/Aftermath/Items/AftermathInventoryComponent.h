#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Items/AftermathGrid.h"
#include "AftermathInventoryComponent.generated.h"

class AAftermathCharacter;
class AAftermathPickup;

/** One wearable slot: the worn item and the cargo grid it provides (if any). */
USTRUCT(BlueprintType)
struct AFTERMATH_API FAftermathEquipSlot
{
	GENERATED_BODY()

	UPROPERTY()
	EAftermathWearSlot Slot = EAftermathWearSlot::None;

	/** Uid 0 = nothing worn. */
	UPROPERTY()
	FAftermathItemInstance Item;

	/** Sized from the worn item's Wear.Grid. Invalid (0x0) when the item has no storage. */
	UPROPERTY()
	FAftermathGrid Cargo;

	bool IsEmpty() const { return !Item.IsValid(); }
};

/** Where an item is: the worn item of Slot, or an entry inside Slot's cargo. */
struct FAftermathItemLocation
{
	EAftermathWearSlot Slot = EAftermathWearSlot::None;
	bool bWorn = false;
};

/** How to take an item from the ground. */
UENUM()
enum class EAftermathTakeMode : uint8
{
	/** Wear it if the slot is free, otherwise first free spot anywhere. */
	Auto,
	/** Put it at an exact cell of a container. */
	Place,
	/** Wear it in its slot (slot must be free). */
	Equip
};

/**
 * Survivor inventory. Server owns all state; the owning client gets a replicated copy (COND_OwnerOnly)
 * and asks for changes through Server* RPCs. Every RPC re-validates on the server, so a modified client
 * can only request things a legitimate client could do.
 *
 * Model (DayZ-like): storage comes from what you wear. Items with cargo keep their contents when dropped;
 * they only go into another container when empty.
 */
UCLASS(ClassGroup = (Aftermath), meta = (BlueprintSpawnableComponent))
class AFTERMATH_API UAftermathInventoryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UAftermathInventoryComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	DECLARE_MULTICAST_DELEGATE(FOnInventoryChanged);
	/** Fires on the server after each change and on the owning client after each replicated update. */
	FOnInventoryChanged OnChanged;

	// ---- Queries (server and owning client) ----
	const TArray<FAftermathEquipSlot>& GetSlots() const { return Slots; }
	const FAftermathEquipSlot* GetSlot(EAftermathWearSlot Slot) const;
	const FAftermathItemInstance* FindItem(int32 Uid, FAftermathItemLocation* OutLocation = nullptr) const;
	/** Container grid of a slot, null if nothing with storage is worn there. */
	const FAftermathGrid* GetGrid(EAftermathWearSlot Slot) const;
	int32 CountOf(FName Def) const;
	/** Number of distinct items (worn + in cargo). */
	int32 CountItems() const;
	int32 TotalWeightGrams() const;
	/** 0..1 body insulation from worn clothes, reduced by wetness and damage. */
	float GetInsulation() const;
	/** First item (worn or carried) with the given def, 0 if none. */
	int32 FindFirstUid(FName Def) const;
	/** First carried item that has the tag, 0 if none. */
	int32 FindFirstWithTag(FName Tag) const;

	// ---- Server mutations. All return false (and log the reason) without changing anything on failure. ----
	/** Store anywhere (wear if possible). Returns how many of Item.Count could NOT be stored. */
	int32 AddItem(const FAftermathItemInstance& Item, const FAftermathGrid* Cargo = nullptr);
	bool MoveItem(int32 Uid, EAftermathWearSlot ToContainer, int32 X, int32 Y, bool bRotated);
	bool Equip(int32 Uid);
	bool Unequip(EAftermathWearSlot Slot, EAftermathWearSlot ToContainer, int32 X, int32 Y, bool bRotated);
	bool Drop(int32 Uid);
	bool Split(int32 Uid, int32 Amount);
	bool Merge(int32 SourceUid, int32 TargetUid);
	bool TakeFromGround(AAftermathPickup* Pickup, EAftermathTakeMode Mode, EAftermathWearSlot ToContainer = EAftermathWearSlot::None,
		int32 X = 0, int32 Y = 0, bool bRotated = false);

	/** Server: direct access for item use / crafting. Call MarkChanged() after editing. */
	FAftermathItemInstance* FindItemMutable(int32 Uid, FAftermathItemLocation* OutLocation = nullptr);
	/** Server: remove an item (and its cargo if worn). */
	bool RemoveItem(int32 Uid, FAftermathItemInstance& OutItem, FAftermathGrid* OutCargo = nullptr);
	/** Server: take Count off a stack; removes the entry when it reaches 0. */
	bool ConsumeCount(int32 Uid, int32 Count);
	/** Server: spawn a pickup for an item next to the owner. */
	AAftermathPickup* SpawnPickupNearOwner(const FAftermathItemInstance& Item, const FAftermathGrid* Cargo) const;
	/** Server: drop everything on the ground (death). */
	void DropAll();
	/** Server: fresh-spawn clothes and a couple of rags. */
	void GiveStartingLoadout();
	void MarkChanged();

	// ---- Owning client -> server requests ----
	UFUNCTION(Server, Reliable)
	void ServerMoveItem(int32 Uid, EAftermathWearSlot ToContainer, int32 X, int32 Y, bool bRotated);

	UFUNCTION(Server, Reliable)
	void ServerEquip(int32 Uid);

	UFUNCTION(Server, Reliable)
	void ServerUnequip(EAftermathWearSlot Slot, EAftermathWearSlot ToContainer, int32 X, int32 Y, bool bRotated);

	UFUNCTION(Server, Reliable)
	void ServerDrop(int32 Uid);

	UFUNCTION(Server, Reliable)
	void ServerSplit(int32 Uid, int32 Amount);

	UFUNCTION(Server, Reliable)
	void ServerMerge(int32 SourceUid, int32 TargetUid);

	UFUNCTION(Server, Reliable)
	void ServerTakeFromGround(AAftermathPickup* Pickup, EAftermathTakeMode Mode, EAftermathWearSlot ToContainer, int32 X, int32 Y, bool bRotated);

protected:
	UPROPERTY(ReplicatedUsing = OnRep_Slots)
	TArray<FAftermathEquipSlot> Slots;

	UFUNCTION()
	void OnRep_Slots();

private:
	FAftermathEquipSlot* SlotMutable(EAftermathWearSlot Slot);
	FAftermathGrid* GridMutable(EAftermathWearSlot Slot);
	/** Wear Item in its slot; slot must be empty. Cargo (if given) is kept, otherwise an empty grid is made. */
	bool WearInto(const FAftermathItemInstance& Item, const FAftermathGrid* Cargo);
	/** Grid entry for a carried (not worn) item. */
	FAftermathGridEntry* FindEntryMutable(int32 Uid, EAftermathWearSlot* OutSlot = nullptr);
	bool Reject(const TCHAR* Op, const TCHAR* Reason) const;
	AAftermathCharacter* GetSurvivor() const;
	bool IsServer() const;
};
