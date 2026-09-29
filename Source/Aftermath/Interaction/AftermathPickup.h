#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/AftermathInteractable.h"
#include "Items/AftermathGrid.h"
#include "AftermathPickup.generated.h"

class UStaticMeshComponent;

/**
 * Item lying in the world. Carries a full item instance (count, uses, health...) and, for containers
 * like a dropped backpack, its cargo. Taking it moves everything into the taker's inventory (server).
 *
 * Placed in the map by id (ItemId + Count); the instance is created from the definition on BeginPlay.
 */
UCLASS()
class AFTERMATH_API AAftermathPickup : public AActor, public IAftermathInteractable
{
	GENERATED_BODY()

public:
	AAftermathPickup();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;

	// IAftermathInteractable
	virtual FText GetInteractPrompt(const AAftermathCharacter* User) const override;
	virtual FVector GetInteractLocation() const override;
	virtual bool CanInteract(const AAftermathCharacter* User) const override;
	virtual void Interact(AAftermathCharacter* User) override;

	/** Server: set contents (before or after spawn). */
	void SetItem(const FAftermathItemInstance& InItem, const FAftermathGrid* InCargo = nullptr);
	/** Server: convenience for map placement and tests. */
	void SetItemById(FName InItemId, int32 InCount = 1);

	const FAftermathItemInstance& GetItem() const { return Item; }
	const FAftermathGrid& GetCargo() const { return Cargo; }
	FName GetItemId() const { return Item.Def; }
	FText GetDisplayName() const;

	/** Server: called by the inventory after it took the item. Destroys the actor once. */
	void MarkTaken();
	bool IsTaken() const { return bTaken; }

	/** Server: after a partial take (stack didn't fully fit), shrink what's left. */
	void SetRemainingCount(int32 Count);

protected:
	UPROPERTY(VisibleAnywhere, Category = "Pickup")
	TObjectPtr<UStaticMeshComponent> Mesh;

	/** Set in the editor / map script; used to build Item on BeginPlay when Item isn't set yet. */
	UPROPERTY(EditAnywhere, Category = "Pickup")
	FName ItemId = TEXT("Bandage");

	UPROPERTY(EditAnywhere, Category = "Pickup")
	int32 Count = 1;

	UPROPERTY(ReplicatedUsing = OnRep_Item, VisibleInstanceOnly, Category = "Pickup")
	FAftermathItemInstance Item;

	/** Only for containers (dropped backpack...). Not replicated: nobody sees inside until they take it. */
	UPROPERTY(VisibleInstanceOnly, Category = "Pickup")
	FAftermathGrid Cargo;

	UFUNCTION()
	void OnRep_Item();

private:
	void UpdateVisual();
	bool bTaken = false;
};
