#include "Items/AftermathInventoryComponent.h"

#include "Aftermath.h"
#include "Engine/World.h"
#include "Interaction/AftermathPickup.h"
#include "Items/AftermathItemDatabase.h"
#include "Net/UnrealNetwork.h"
#include "Player/AftermathCharacter.h"

namespace
{
	constexpr EAftermathWearSlot AllSlots[] = {
		EAftermathWearSlot::Torso, EAftermathWearSlot::Legs, EAftermathWearSlot::Feet, EAftermathWearSlot::Back,
		EAftermathWearSlot::Head, EAftermathWearSlot::Vest, EAftermathWearSlot::Hands };

	/** Container search order for "put it anywhere": pockets first, backpack last (like DayZ). */
	constexpr EAftermathWearSlot StoreOrder[] = {
		EAftermathWearSlot::Torso, EAftermathWearSlot::Legs, EAftermathWearSlot::Vest, EAftermathWearSlot::Back };

	/** Server allows a little slack over the client's own check (latency, movement). */
	constexpr float GroundReach = 280.f;
}

UAftermathInventoryComponent::UAftermathInventoryComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);

	for (EAftermathWearSlot S : AllSlots)
	{
		FAftermathEquipSlot& Slot = Slots.AddDefaulted_GetRef();
		Slot.Slot = S;
	}
}

void UAftermathInventoryComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	// Nobody else learns what you carry. What you WEAR is visible via the character's appearance later.
	DOREPLIFETIME_CONDITION(UAftermathInventoryComponent, Slots, COND_OwnerOnly);
}

void UAftermathInventoryComponent::OnRep_Slots()
{
	OnChanged.Broadcast();
}

void UAftermathInventoryComponent::MarkChanged()
{
	if (AActor* Owner = GetOwner())
	{
		Owner->ForceNetUpdate();
	}
	OnChanged.Broadcast();
}

bool UAftermathInventoryComponent::IsServer() const
{
	const AActor* Owner = GetOwner();
	return Owner && Owner->HasAuthority();
}

AAftermathCharacter* UAftermathInventoryComponent::GetSurvivor() const
{
	return Cast<AAftermathCharacter>(GetOwner());
}

bool UAftermathInventoryComponent::Reject(const TCHAR* Op, const TCHAR* Reason) const
{
	UE_LOG(LogAftermath, Log, TEXT("[Aftermath] InventoryRejected op=%s reason=%s owner=%s"), Op, Reason, *GetNameSafe(GetOwner()));
	return false;
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

const FAftermathEquipSlot* UAftermathInventoryComponent::GetSlot(EAftermathWearSlot Slot) const
{
	return Slots.FindByPredicate([Slot](const FAftermathEquipSlot& S) { return S.Slot == Slot; });
}

FAftermathEquipSlot* UAftermathInventoryComponent::SlotMutable(EAftermathWearSlot Slot)
{
	return Slots.FindByPredicate([Slot](const FAftermathEquipSlot& S) { return S.Slot == Slot; });
}

const FAftermathGrid* UAftermathInventoryComponent::GetGrid(EAftermathWearSlot Slot) const
{
	const FAftermathEquipSlot* S = GetSlot(Slot);
	return (S && !S->IsEmpty() && S->Cargo.IsValid()) ? &S->Cargo : nullptr;
}

FAftermathGrid* UAftermathInventoryComponent::GridMutable(EAftermathWearSlot Slot)
{
	FAftermathEquipSlot* S = SlotMutable(Slot);
	return (S && !S->IsEmpty() && S->Cargo.IsValid()) ? &S->Cargo : nullptr;
}

const FAftermathItemInstance* UAftermathInventoryComponent::FindItem(int32 Uid, FAftermathItemLocation* OutLocation) const
{
	return const_cast<UAftermathInventoryComponent*>(this)->FindItemMutable(Uid, OutLocation);
}

FAftermathItemInstance* UAftermathInventoryComponent::FindItemMutable(int32 Uid, FAftermathItemLocation* OutLocation)
{
	if (Uid == 0)
	{
		return nullptr;
	}
	for (FAftermathEquipSlot& S : Slots)
	{
		if (S.Item.Uid == Uid)
		{
			if (OutLocation) { OutLocation->Slot = S.Slot; OutLocation->bWorn = true; }
			return &S.Item;
		}
		if (FAftermathGridEntry* E = S.Cargo.FindEntry(Uid))
		{
			if (OutLocation) { OutLocation->Slot = S.Slot; OutLocation->bWorn = false; }
			return &E->Item;
		}
	}
	return nullptr;
}

FAftermathGridEntry* UAftermathInventoryComponent::FindEntryMutable(int32 Uid, EAftermathWearSlot* OutSlot)
{
	for (FAftermathEquipSlot& S : Slots)
	{
		if (FAftermathGridEntry* E = S.Cargo.FindEntry(Uid))
		{
			if (OutSlot) { *OutSlot = S.Slot; }
			return E;
		}
	}
	return nullptr;
}

int32 UAftermathInventoryComponent::CountOf(FName Def) const
{
	int32 N = 0;
	for (const FAftermathEquipSlot& S : Slots)
	{
		if (S.Item.IsValid() && S.Item.Def == Def)
		{
			N += S.Item.Count;
		}
		N += S.Cargo.CountOf(Def);
	}
	return N;
}

int32 UAftermathInventoryComponent::CountItems() const
{
	int32 N = 0;
	for (const FAftermathEquipSlot& S : Slots)
	{
		N += (S.Item.IsValid() ? 1 : 0) + S.Cargo.Entries.Num();
	}
	return N;
}

int32 UAftermathInventoryComponent::TotalWeightGrams() const
{
	int32 Grams = 0;
	for (const FAftermathEquipSlot& S : Slots)
	{
		if (S.Item.IsValid())
		{
			Grams += AftermathItems::InstanceWeight(S.Item);
		}
		Grams += S.Cargo.TotalWeight();
	}
	return Grams;
}

float UAftermathInventoryComponent::GetInsulation() const
{
	float Total = 0.f;
	for (const FAftermathEquipSlot& S : Slots)
	{
		const FAftermathItemDef* Def = S.Item.IsValid() ? DefOf(S.Item.Def) : nullptr;
		if (!Def)
		{
			continue;
		}
		// Wet clothes keep ~25% of their warmth; ruined clothes none.
		const float WetFactor = FMath::Lerp(1.f, 0.25f, FMath::Clamp(S.Item.Wetness, 0.f, 1.f));
		const float DamageFactor = S.Item.Health <= 0.f ? 0.f : FMath::Lerp(0.5f, 1.f, S.Item.Health);
		Total += Def->Wear.Insulation * WetFactor * DamageFactor;
	}
	return FMath::Clamp(Total, 0.f, 1.f);
}

int32 UAftermathInventoryComponent::FindFirstUid(FName Def) const
{
	for (const FAftermathEquipSlot& S : Slots)
	{
		if (S.Item.IsValid() && S.Item.Def == Def)
		{
			return S.Item.Uid;
		}
		for (const FAftermathGridEntry& E : S.Cargo.Entries)
		{
			if (E.Item.Def == Def)
			{
				return E.Item.Uid;
			}
		}
	}
	return 0;
}

int32 UAftermathInventoryComponent::FindFirstWithTag(FName Tag) const
{
	for (const FAftermathEquipSlot& S : Slots)
	{
		for (const FAftermathGridEntry& E : S.Cargo.Entries)
		{
			const FAftermathItemDef* Def = DefOf(E.Item.Def);
			if (Def && Def->HasTag(Tag) && E.Item.Health > 0.f)
			{
				return E.Item.Uid;
			}
		}
	}
	return 0;
}

// ---------------------------------------------------------------------------
// Mutations
// ---------------------------------------------------------------------------

bool UAftermathInventoryComponent::WearInto(const FAftermathItemInstance& Item, const FAftermathGrid* Cargo)
{
	const FAftermathItemDef* Def = DefOf(Item.Def);
	if (!Def || !Def->IsWearable())
	{
		return false;
	}
	FAftermathEquipSlot* Slot = SlotMutable(Def->Wear.Slot);
	if (!Slot || !Slot->IsEmpty())
	{
		return false;
	}
	Slot->Item = Item;
	Slot->Item.Count = 1;
	if (Cargo && Cargo->IsValid())
	{
		Slot->Cargo = *Cargo;
	}
	else
	{
		Slot->Cargo = Def->HasCargo() ? FAftermathGrid(Def->Wear.Grid.X, Def->Wear.Grid.Y) : FAftermathGrid();
	}
	return true;
}

int32 UAftermathInventoryComponent::AddItem(const FAftermathItemInstance& Item, const FAftermathGrid* Cargo)
{
	if (!IsServer() || !Item.IsValid() || !DefOf(Item.Def))
	{
		return Item.Count;
	}

	// Wear first if the slot is free.
	if (WearInto(Item, Cargo))
	{
		MarkChanged();
		return 0;
	}

	// A container with contents can't go into another container.
	if (Cargo && !Cargo->IsEmpty())
	{
		return Item.Count;
	}

	FAftermathItemInstance Rest = Item;
	for (EAftermathWearSlot S : StoreOrder)
	{
		if (FAftermathGrid* G = GridMutable(S))
		{
			Rest.Count = G->AddAnywhere(Rest);
			if (Rest.Count <= 0)
			{
				break;
			}
			// The placed remainder (if any) took Rest.Uid; the leftover needs a new one.
			if (G->FindEntry(Rest.Uid))
			{
				Rest.Uid = AftermathItems::NextUid();
			}
		}
	}

	if (Rest.Count != Item.Count)
	{
		MarkChanged();
	}
	return FMath::Max(0, Rest.Count);
}

bool UAftermathInventoryComponent::MoveItem(int32 Uid, EAftermathWearSlot ToContainer, int32 X, int32 Y, bool bRotated)
{
	if (!IsServer())
	{
		return false;
	}
	EAftermathWearSlot FromContainer = EAftermathWearSlot::None;
	FAftermathGridEntry* Entry = FindEntryMutable(Uid, &FromContainer);
	FAftermathGrid* To = GridMutable(ToContainer);
	if (!Entry)
	{
		return Reject(TEXT("move"), TEXT("not carried"));
	}
	if (!To)
	{
		return Reject(TEXT("move"), TEXT("no container"));
	}

	if (FromContainer == ToContainer)
	{
		if (!To->MoveWithin(Uid, X, Y, bRotated))
		{
			return Reject(TEXT("move"), TEXT("blocked"));
		}
	}
	else
	{
		if (!To->CanPlace(Entry->Item, X, Y, bRotated))
		{
			return Reject(TEXT("move"), TEXT("blocked"));
		}
		FAftermathItemInstance Moving;
		GridMutable(FromContainer)->Remove(Uid, Moving);
		To->Place(Moving, X, Y, bRotated);
	}
	MarkChanged();
	return true;
}

bool UAftermathInventoryComponent::Equip(int32 Uid)
{
	if (!IsServer())
	{
		return false;
	}
	EAftermathWearSlot From = EAftermathWearSlot::None;
	FAftermathGridEntry* Entry = FindEntryMutable(Uid, &From);
	const FAftermathItemDef* Def = Entry ? DefOf(Entry->Item.Def) : nullptr;
	if (!Def || !Def->IsWearable())
	{
		return Reject(TEXT("equip"), TEXT("not wearable"));
	}
	const FAftermathEquipSlot* Target = GetSlot(Def->Wear.Slot);
	if (!Target || !Target->IsEmpty())
	{
		return Reject(TEXT("equip"), TEXT("slot occupied"));
	}
	FAftermathItemInstance Item;
	GridMutable(From)->Remove(Uid, Item);
	WearInto(Item, nullptr);
	MarkChanged();
	return true;
}

bool UAftermathInventoryComponent::Unequip(EAftermathWearSlot SlotId, EAftermathWearSlot ToContainer, int32 X, int32 Y, bool bRotated)
{
	if (!IsServer())
	{
		return false;
	}
	FAftermathEquipSlot* Slot = SlotMutable(SlotId);
	if (!Slot || Slot->IsEmpty())
	{
		return Reject(TEXT("unequip"), TEXT("nothing worn"));
	}
	if (!Slot->Cargo.IsEmpty())
	{
		// Same as DayZ: take it off onto the ground instead (Drop), or empty it first.
		return Reject(TEXT("unequip"), TEXT("container not empty"));
	}
	if (ToContainer == SlotId)
	{
		return Reject(TEXT("unequip"), TEXT("into itself"));
	}
	FAftermathGrid* To = GridMutable(ToContainer);
	if (!To || !To->CanPlace(Slot->Item, X, Y, bRotated))
	{
		return Reject(TEXT("unequip"), TEXT("blocked"));
	}
	const FAftermathItemInstance Item = Slot->Item;
	Slot->Item = FAftermathItemInstance();
	Slot->Cargo = FAftermathGrid();
	To->Place(Item, X, Y, bRotated);
	MarkChanged();
	return true;
}

bool UAftermathInventoryComponent::RemoveItem(int32 Uid, FAftermathItemInstance& OutItem, FAftermathGrid* OutCargo)
{
	for (FAftermathEquipSlot& S : Slots)
	{
		if (S.Item.Uid == Uid && Uid != 0)
		{
			OutItem = S.Item;
			if (OutCargo)
			{
				*OutCargo = S.Cargo;
			}
			S.Item = FAftermathItemInstance();
			S.Cargo = FAftermathGrid();
			return true;
		}
		if (S.Cargo.Remove(Uid, OutItem))
		{
			if (OutCargo)
			{
				*OutCargo = FAftermathGrid();
			}
			return true;
		}
	}
	return false;
}

bool UAftermathInventoryComponent::ConsumeCount(int32 Uid, int32 InCount)
{
	FAftermathItemInstance* Item = FindItemMutable(Uid);
	if (!Item || InCount <= 0 || Item->Count < InCount)
	{
		return false;
	}
	Item->Count -= InCount;
	if (Item->Count <= 0)
	{
		FAftermathItemInstance Dummy;
		RemoveItem(Uid, Dummy);
	}
	MarkChanged();
	return true;
}

AAftermathPickup* UAftermathInventoryComponent::SpawnPickupNearOwner(const FAftermathItemInstance& Item, const FAftermathGrid* Cargo) const
{
	const AActor* Owner = GetOwner();
	UWorld* World = GetWorld();
	if (!Owner || !World)
	{
		return nullptr;
	}

	// In front of the feet, slightly scattered so a dropped pile doesn't stack into one box.
	const FVector Forward = Owner->GetActorForwardVector().GetSafeNormal2D();
	const FVector Jitter(FMath::FRandRange(-25.f, 25.f), FMath::FRandRange(-25.f, 25.f), 0.f);
	FVector Loc = Owner->GetActorLocation() + Forward * 60.f + Jitter;

	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(AftermathDrop), false, Owner);
	if (World->LineTraceSingleByChannel(Hit, Loc + FVector(0, 0, 50.f), Loc - FVector(0, 0, 300.f), ECC_Visibility, Params))
	{
		Loc = Hit.ImpactPoint + FVector(0, 0, 5.f);
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.bDeferConstruction = true;
	AAftermathPickup* Pickup = World->SpawnActor<AAftermathPickup>(Loc, FRotator(0.f, FMath::FRandRange(0.f, 360.f), 0.f), SpawnParams);
	if (Pickup)
	{
		Pickup->SetItem(Item, Cargo);
		Pickup->FinishSpawning(FTransform(Pickup->GetActorRotation(), Loc));
	}
	return Pickup;
}

bool UAftermathInventoryComponent::Drop(int32 Uid)
{
	if (!IsServer())
	{
		return false;
	}
	FAftermathItemInstance Item;
	FAftermathGrid Cargo;
	if (!RemoveItem(Uid, Item, &Cargo))
	{
		return Reject(TEXT("drop"), TEXT("not carried"));
	}
	SpawnPickupNearOwner(Item, &Cargo);
	UE_LOG(LogAftermath, Display, TEXT("[Aftermath] ItemDropped item=%s count=%d by=%s"), *Item.Def.ToString(), Item.Count, *GetNameSafe(GetOwner()));
	MarkChanged();
	return true;
}

bool UAftermathInventoryComponent::Split(int32 Uid, int32 Amount)
{
	if (!IsServer())
	{
		return false;
	}
	EAftermathWearSlot From = EAftermathWearSlot::None;
	if (!FindEntryMutable(Uid, &From))
	{
		return Reject(TEXT("split"), TEXT("not carried"));
	}
	if (!GridMutable(From)->Split(Uid, Amount, AftermathItems::NextUid()))
	{
		return Reject(TEXT("split"), TEXT("no room or bad amount"));
	}
	MarkChanged();
	return true;
}

bool UAftermathInventoryComponent::Merge(int32 SourceUid, int32 TargetUid)
{
	if (!IsServer())
	{
		return false;
	}
	EAftermathWearSlot SrcSlot = EAftermathWearSlot::None;
	EAftermathWearSlot DstSlot = EAftermathWearSlot::None;
	FAftermathGridEntry* Src = FindEntryMutable(SourceUid, &SrcSlot);
	FAftermathGridEntry* Dst = FindEntryMutable(TargetUid, &DstSlot);
	const FAftermathItemDef* Def = Src ? DefOf(Src->Item.Def) : nullptr;
	if (!Src || !Dst || !Def || !Def->IsStackable() || Src->Item.Def != Dst->Item.Def || SourceUid == TargetUid)
	{
		return Reject(TEXT("merge"), TEXT("not mergeable"));
	}

	const int32 Move = FMath::Min(Src->Item.Count, Def->MaxStack - Dst->Item.Count);
	if (Move <= 0)
	{
		return Reject(TEXT("merge"), TEXT("target full"));
	}
	Dst->Item.Count += Move;
	Src->Item.Count -= Move;
	if (Src->Item.Count <= 0)
	{
		FAftermathItemInstance Dummy;
		GridMutable(SrcSlot)->Remove(SourceUid, Dummy);
	}
	MarkChanged();
	return true;
}

bool UAftermathInventoryComponent::TakeFromGround(AAftermathPickup* Pickup, EAftermathTakeMode Mode, EAftermathWearSlot ToContainer,
	int32 X, int32 Y, bool bRotated)
{
	if (!IsServer())
	{
		return false;
	}
	if (!IsValid(Pickup) || Pickup->IsTaken() || !Pickup->GetItem().IsValid())
	{
		return Reject(TEXT("take"), TEXT("gone"));
	}
	const AActor* Owner = GetOwner();
	if (!Owner || FVector::Dist(Owner->GetActorLocation(), Pickup->GetActorLocation()) > GroundReach)
	{
		UE_LOG(LogAftermath, Warning, TEXT("[Aftermath] InteractRejected reason=range target=%s by=%s dist=%.0f"),
			*Pickup->GetName(), *GetNameSafe(Owner), Owner ? FVector::Dist(Owner->GetActorLocation(), Pickup->GetActorLocation()) : -1.f);
		return false;
	}

	const FAftermathItemInstance Item = Pickup->GetItem();
	const FAftermathGrid& Cargo = Pickup->GetCargo();

	bool bTookAll = false;
	switch (Mode)
	{
	case EAftermathTakeMode::Equip:
		if (!WearInto(Item, &Cargo))
		{
			return Reject(TEXT("take"), TEXT("cannot wear"));
		}
		bTookAll = true;
		break;

	case EAftermathTakeMode::Place:
	{
		if (!Cargo.IsEmpty())
		{
			return Reject(TEXT("take"), TEXT("container not empty"));
		}
		FAftermathGrid* To = GridMutable(ToContainer);
		if (!To || !To->Place(Item, X, Y, bRotated))
		{
			return Reject(TEXT("take"), TEXT("blocked"));
		}
		bTookAll = true;
		break;
	}

	case EAftermathTakeMode::Auto:
	default:
	{
		const int32 Left = AddItem(Item, &Cargo);
		if (Left >= Item.Count)
		{
			return Reject(TEXT("take"), TEXT("no room"));
		}
		if (Left > 0)
		{
			Pickup->SetRemainingCount(Left);
			MarkChanged();
			UE_LOG(LogAftermath, Display, TEXT("[Aftermath] ItemPicked item=%s count=%d partial=1 by=%s"),
				*Item.Def.ToString(), Item.Count - Left, *GetNameSafe(Owner));
			return true;
		}
		bTookAll = true;
		break;
	}
	}

	if (bTookAll)
	{
		Pickup->MarkTaken();
		MarkChanged();
		UE_LOG(LogAftermath, Display, TEXT("[Aftermath] ItemPicked item=%s count=%d by=%s"), *Item.Def.ToString(), Item.Count, *GetNameSafe(Owner));
	}
	return true;
}

void UAftermathInventoryComponent::DropAll()
{
	if (!IsServer())
	{
		return;
	}
	// Worn items drop with their cargo inside, loose cargo of grids drops separately only if not in a worn container.
	for (FAftermathEquipSlot& S : Slots)
	{
		if (S.Item.IsValid())
		{
			SpawnPickupNearOwner(S.Item, &S.Cargo);
		}
		S.Item = FAftermathItemInstance();
		S.Cargo = FAftermathGrid();
	}
	MarkChanged();
}

void UAftermathInventoryComponent::GiveStartingLoadout()
{
	if (!IsServer())
	{
		return;
	}
	const UAftermathItemDatabase* DB = UAftermathItemDatabase::Get();
	if (!DB)
	{
		return;
	}
	auto Give = [this, DB](FName Id, int32 InCount)
	{
		if (const FAftermathItemDef* Def = DB->Find(Id))
		{
			AddItem(AftermathItems::MakeInstance(*Def, InCount));
		}
	};
	// DayZ fresh spawn: basic clothes, a light source is stage 5; rags so a first bleed isn't a death sentence.
	Give(TEXT("TShirt"), 1);
	Give(TEXT("Jeans"), 1);
	Give(TEXT("Sneakers"), 1);
	Give(TEXT("Rag"), 2);
	Give(TEXT("Apple"), 1);
}

// ---------------------------------------------------------------------------
// RPCs
// ---------------------------------------------------------------------------

void UAftermathInventoryComponent::ServerMoveItem_Implementation(int32 Uid, EAftermathWearSlot ToContainer, int32 X, int32 Y, bool bRotated)
{
	MoveItem(Uid, ToContainer, X, Y, bRotated);
}

void UAftermathInventoryComponent::ServerEquip_Implementation(int32 Uid)
{
	Equip(Uid);
}

void UAftermathInventoryComponent::ServerUnequip_Implementation(EAftermathWearSlot Slot, EAftermathWearSlot ToContainer, int32 X, int32 Y, bool bRotated)
{
	Unequip(Slot, ToContainer, X, Y, bRotated);
}

void UAftermathInventoryComponent::ServerDrop_Implementation(int32 Uid)
{
	Drop(Uid);
}

void UAftermathInventoryComponent::ServerSplit_Implementation(int32 Uid, int32 Amount)
{
	Split(Uid, Amount);
}

void UAftermathInventoryComponent::ServerMerge_Implementation(int32 SourceUid, int32 TargetUid)
{
	Merge(SourceUid, TargetUid);
}

void UAftermathInventoryComponent::ServerTakeFromGround_Implementation(AAftermathPickup* Pickup, EAftermathTakeMode Mode,
	EAftermathWearSlot ToContainer, int32 X, int32 Y, bool bRotated)
{
	TakeFromGround(Pickup, Mode, ToContainer, X, Y, bRotated);
}
