#include "Items/AftermathItemActions.h"

#include "Aftermath.h"
#include "Items/AftermathInventoryComponent.h"
#include "Items/AftermathItemDatabase.h"
#include "Player/AftermathCharacter.h"
#include "Survival/AftermathSurvivalComponent.h"

namespace
{
	const FAftermathItemDef* DefOf(FName Id)
	{
		const UAftermathItemDatabase* DB = UAftermathItemDatabase::Get();
		return DB ? DB->Find(Id) : nullptr;
	}

	EAftermathActionResult Log(const AAftermathCharacter& Who, const TCHAR* Op, FName Item, EAftermathActionResult R)
	{
		UE_LOG(LogAftermath, Display, TEXT("[Aftermath] ItemAction op=%s item=%s result=%s by=%s"),
			Op, *Item.ToString(), AftermathItemActions::ResultToString(R), *Who.GetName());
		return R;
	}

	/** Spend one portion (or one of a stack). Removes the item when used up. */
	void SpendOne(UAftermathInventoryComponent& Inv, int32 Uid, const FAftermathItemDef& Def)
	{
		FAftermathItemInstance* Item = Inv.FindItemMutable(Uid);
		if (!Item)
		{
			return;
		}
		if (Def.Uses > 0)
		{
			Item->Uses = FMath::Max(0, Item->Uses - 1);
			// Bottles stay (empty) so they can be refilled later; everything else is consumed.
			const bool bKeepEmpty = Def.Use.Action == EAftermathUseAction::Drink && Def.Id == TEXT("WaterBottle");
			if (Item->Uses <= 0 && !bKeepEmpty)
			{
				FAftermathItemInstance Gone;
				Inv.RemoveItem(Uid, Gone);
			}
			Inv.MarkChanged();
		}
		else
		{
			Inv.ConsumeCount(Uid, 1);
		}
	}

	/** Replace a carried item in place with a different def (e.g. can -> opened can). Falls back to AddItem. */
	bool Transform(UAftermathInventoryComponent& Inv, int32 Uid, const FAftermathItemDef& NewDef)
	{
		FAftermathItemInstance* Item = Inv.FindItemMutable(Uid);
		if (!Item)
		{
			return false;
		}
		const FAftermathItemDef* OldDef = DefOf(Item->Def);
		if (OldDef && OldDef->Size == NewDef.Size)
		{
			Item->Def = NewDef.Id;
			Item->Uses = NewDef.Uses;
			Item->Count = 1;
			Inv.MarkChanged();
			return true;
		}
		FAftermathItemInstance Old;
		Inv.RemoveItem(Uid, Old);
		FAftermathItemInstance Fresh = AftermathItems::MakeInstance(NewDef);
		Fresh.Health = Old.Health;
		if (Inv.AddItem(Fresh) > 0)
		{
			Inv.SpawnPickupNearOwner(Fresh, nullptr);
		}
		return true;
	}

	/** Chip a tool's health a little per use; ruined tools stop working. */
	void WearTool(UAftermathInventoryComponent& Inv, int32 ToolUid, float Amount)
	{
		if (FAftermathItemInstance* Tool = Inv.FindItemMutable(ToolUid))
		{
			Tool->Health = FMath::Max(0.f, Tool->Health - Amount);
			Inv.MarkChanged();
		}
	}

	EAftermathActionResult OpenCan(AAftermathCharacter& Who, UAftermathInventoryComponent& Inv, int32 CanUid, int32 ToolUid)
	{
		const FAftermathItemInstance* Can = Inv.FindItem(CanUid);
		const FAftermathItemDef* CanDef = Can ? DefOf(Can->Def) : nullptr;
		const FAftermathItemDef* Opened = CanDef ? DefOf(CanDef->OpensInto) : nullptr;
		if (!Opened)
		{
			return Log(Who, TEXT("open"), Can ? Can->Def : NAME_None, EAftermathActionResult::NotUsable);
		}
		const FAftermathItemInstance* Tool = Inv.FindItem(ToolUid);
		const FAftermathItemDef* ToolDef = Tool ? DefOf(Tool->Def) : nullptr;
		if (!ToolDef || !ToolDef->HasTag(AftermathItems::TagOpensCans) || Tool->Health <= 0.f)
		{
			return Log(Who, TEXT("open"), CanDef->Id, EAftermathActionResult::NeedsTool);
		}
		const FName CanId = CanDef->Id;
		Transform(Inv, CanUid, *Opened);
		WearTool(Inv, ToolUid, 0.02f);
		return Log(Who, TEXT("open"), CanId, EAftermathActionResult::Ok);
	}
}

namespace AftermathItemActions
{
	const TCHAR* ResultToString(EAftermathActionResult R)
	{
		switch (R)
		{
		case EAftermathActionResult::Ok:             return TEXT("ok");
		case EAftermathActionResult::NotFound:       return TEXT("not_found");
		case EAftermathActionResult::NotUsable:      return TEXT("not_usable");
		case EAftermathActionResult::NothingToTreat: return TEXT("nothing_to_treat");
		case EAftermathActionResult::NeedsTool:      return TEXT("needs_tool");
		case EAftermathActionResult::NoRoom:         return TEXT("no_room");
		case EAftermathActionResult::Dead:           return TEXT("dead");
		default:                                     return TEXT("invalid");
		}
	}

	EAftermathActionResult Use(AAftermathCharacter& Who, int32 Uid)
	{
		UAftermathInventoryComponent* Inv = Who.GetInventory();
		UAftermathSurvivalComponent* Survival = Who.GetSurvival();
		if (!Inv || !Survival || !Who.HasAuthority())
		{
			return EAftermathActionResult::Invalid;
		}
		if (Survival->IsDead())
		{
			return EAftermathActionResult::Dead;
		}

		FAftermathItemLocation Loc;
		const FAftermathItemInstance* Item = Inv->FindItem(Uid, &Loc);
		const FAftermathItemDef* Def = Item ? DefOf(Item->Def) : nullptr;
		if (!Def)
		{
			return Log(Who, TEXT("use"), NAME_None, EAftermathActionResult::NotFound);
		}
		const FName Id = Def->Id;

		// Clothing in cargo: "use" = put it on.
		if (Def->IsWearable() && !Loc.bWorn)
		{
			return Log(Who, TEXT("wear"), Id, Inv->Equip(Uid) ? EAftermathActionResult::Ok : EAftermathActionResult::NoRoom);
		}

		// Sealed can: open it with whatever tool you carry.
		if (!Def->OpensInto.IsNone())
		{
			const int32 Tool = Inv->FindFirstWithTag(AftermathItems::TagOpensCans);
			if (Tool == 0)
			{
				return Log(Who, TEXT("open"), Id, EAftermathActionResult::NeedsTool);
			}
			return OpenCan(Who, *Inv, Uid, Tool);
		}

		const FAftermathUseDef& U = Def->Use;
		switch (U.Action)
		{
		case EAftermathUseAction::Eat:
		case EAftermathUseAction::Drink:
			if (Def->Uses > 0 && Item->Uses <= 0)
			{
				return Log(Who, TEXT("consume"), Id, EAftermathActionResult::NotUsable); // empty bottle
			}
			// Ruined food is spoiled: always poisons.
			Survival->Eat(U.Energy, U.Water, Item->Health <= 0.f ? 1.f : U.PoisonChance);
			SpendOne(*Inv, Uid, *Def);
			return Log(Who, TEXT("consume"), Id, EAftermathActionResult::Ok);

		case EAftermathUseAction::Bandage:
			if (!Survival->Bandage(U.InfectionChance))
			{
				return Log(Who, TEXT("bandage"), Id, EAftermathActionResult::NothingToTreat);
			}
			SpendOne(*Inv, Uid, *Def);
			return Log(Who, TEXT("bandage"), Id, EAftermathActionResult::Ok);

		case EAftermathUseAction::Splint:
			if (!Survival->Splint())
			{
				return Log(Who, TEXT("splint"), Id, EAftermathActionResult::NothingToTreat);
			}
			SpendOne(*Inv, Uid, *Def);
			return Log(Who, TEXT("splint"), Id, EAftermathActionResult::Ok);

		case EAftermathUseAction::Medicine:
		{
			bool bTreated = false;
			if (U.CuresSickness) { bTreated |= Survival->CureSickness(); }
			if (U.CuresPoisoning) { bTreated |= Survival->CurePoisoning(); }
			if (!bTreated)
			{
				return Log(Who, TEXT("medicine"), Id, EAftermathActionResult::NothingToTreat);
			}
			SpendOne(*Inv, Uid, *Def);
			return Log(Who, TEXT("medicine"), Id, EAftermathActionResult::Ok);
		}

		default:
			return Log(Who, TEXT("use"), Id, EAftermathActionResult::NotUsable);
		}
	}

	EAftermathActionResult Combine(AAftermathCharacter& Who, int32 SourceUid, int32 TargetUid)
	{
		UAftermathInventoryComponent* Inv = Who.GetInventory();
		if (!Inv || !Who.HasAuthority() || SourceUid == TargetUid)
		{
			return EAftermathActionResult::Invalid;
		}
		FAftermathItemLocation SrcLoc, DstLoc;
		const FAftermathItemInstance* Src = Inv->FindItem(SourceUid, &SrcLoc);
		const FAftermathItemInstance* Dst = Inv->FindItem(TargetUid, &DstLoc);
		const FAftermathItemDef* SrcDef = Src ? DefOf(Src->Def) : nullptr;
		const FAftermathItemDef* DstDef = Dst ? DefOf(Dst->Def) : nullptr;
		if (!SrcDef || !DstDef)
		{
			return Log(Who, TEXT("combine"), NAME_None, EAftermathActionResult::NotFound);
		}

		// Same stackable -> merge (drag a stack onto a stack).
		if (SrcDef == DstDef && SrcDef->IsStackable())
		{
			return Log(Who, TEXT("merge"), SrcDef->Id, Inv->Merge(SourceUid, TargetUid) ? EAftermathActionResult::Ok : EAftermathActionResult::NoRoom);
		}

		// Tool + can (either order).
		if (SrcDef->HasTag(AftermathItems::TagOpensCans) && !DstDef->OpensInto.IsNone())
		{
			return OpenCan(Who, *Inv, TargetUid, SourceUid);
		}
		if (DstDef->HasTag(AftermathItems::TagOpensCans) && !SrcDef->OpensInto.IsNone())
		{
			return OpenCan(Who, *Inv, SourceUid, TargetUid);
		}

		// Cutting tool + tearable clothing (not worn) -> rags.
		const bool bSrcCuts = SrcDef->HasTag(AftermathItems::TagCuts);
		const bool bDstCuts = DstDef->HasTag(AftermathItems::TagCuts);
		if ((bSrcCuts && DstDef->HasTag(AftermathItems::TagTearable)) || (bDstCuts && SrcDef->HasTag(AftermathItems::TagTearable)))
		{
			const int32 ClothUid = bSrcCuts ? TargetUid : SourceUid;
			const int32 KnifeUid = bSrcCuts ? SourceUid : TargetUid;
			const FAftermathItemLocation& ClothLoc = bSrcCuts ? DstLoc : SrcLoc;
			const FAftermathItemDef* ClothDef = bSrcCuts ? DstDef : SrcDef;
			const FAftermathItemInstance* Knife = Inv->FindItem(KnifeUid);
			if (ClothLoc.bWorn)
			{
				return Log(Who, TEXT("tear"), ClothDef->Id, EAftermathActionResult::NotUsable); // take it off first
			}
			if (!Knife || Knife->Health <= 0.f)
			{
				return Log(Who, TEXT("tear"), ClothDef->Id, EAftermathActionResult::NeedsTool);
			}
			const FAftermathItemDef* RagDef = DefOf(TEXT("Rag"));
			if (!RagDef)
			{
				return EAftermathActionResult::Invalid;
			}
			// Bigger garments give more rags (one per 2 cells, at least 2), ruined ones give fewer.
			const float ClothHealth = Inv->FindItem(ClothUid)->Health;
			const int32 Rags = FMath::Max(1, FMath::RoundToInt(FMath::Max(2, ClothDef->Size.X * ClothDef->Size.Y / 2) * FMath::Max(0.3f, ClothHealth)));
			FAftermathItemInstance Removed;
			Inv->RemoveItem(ClothUid, Removed);
			FAftermathItemInstance RagStack = AftermathItems::MakeInstance(*RagDef, Rags);
			const int32 Left = Inv->AddItem(RagStack);
			if (Left > 0)
			{
				RagStack.Uid = AftermathItems::NextUid();
				RagStack.Count = Left;
				Inv->SpawnPickupNearOwner(RagStack, nullptr);
			}
			WearTool(*Inv, KnifeUid, 0.05f);
			return Log(Who, TEXT("tear"), ClothDef->Id, EAftermathActionResult::Ok);
		}

		// Ammo + magazine -> load as many rounds as fit.
		const bool bSrcMag = SrcDef->IsMagazine();
		const bool bDstMag = DstDef->IsMagazine();
		if ((bDstMag && DstDef->Magazine.Ammo == SrcDef->Id) || (bSrcMag && SrcDef->Magazine.Ammo == DstDef->Id))
		{
			const int32 MagUid = bDstMag ? TargetUid : SourceUid;
			const int32 AmmoUid = bDstMag ? SourceUid : TargetUid;
			const FAftermathItemDef* MagDef = bDstMag ? DstDef : SrcDef;
			FAftermathItemInstance* Mag = Inv->FindItemMutable(MagUid);
			const FAftermathItemInstance* Ammo = Inv->FindItem(AmmoUid);
			const int32 Room = MagDef->Magazine.Capacity - Mag->Loaded;
			const int32 Load = FMath::Min(Room, Ammo->Count);
			if (Load <= 0)
			{
				return Log(Who, TEXT("load"), MagDef->Id, EAftermathActionResult::NoRoom);
			}
			Mag->Loaded += Load;
			Inv->ConsumeCount(AmmoUid, Load);
			Inv->MarkChanged();
			return Log(Who, TEXT("load"), MagDef->Id, EAftermathActionResult::Ok);
		}

		// Magazine + gun -> attach (swap if one is already in).
		if (SrcDef->IsMagazine() && DstDef->AcceptsMagazine(SrcDef->Id))
		{
			return AttachMagazine(Who, TargetUid, SourceUid);
		}
		if (DstDef->IsMagazine() && SrcDef->AcceptsMagazine(DstDef->Id))
		{
			return AttachMagazine(Who, SourceUid, TargetUid);
		}

		return Log(Who, TEXT("combine"), SrcDef->Id, EAftermathActionResult::NotUsable);
	}

	EAftermathActionResult AttachMagazine(AAftermathCharacter& Who, int32 GunUid, int32 MagUid)
	{
		UAftermathInventoryComponent* Inv = Who.GetInventory();
		if (!Inv || !Who.HasAuthority())
		{
			return EAftermathActionResult::Invalid;
		}
		FAftermathItemInstance* Gun = Inv->FindItemMutable(GunUid);
		const FAftermathItemInstance* Mag = Inv->FindItem(MagUid);
		const FAftermathItemDef* GunDef = Gun ? DefOf(Gun->Def) : nullptr;
		const FAftermathItemDef* MagDef = Mag ? DefOf(Mag->Def) : nullptr;
		if (!GunDef || !MagDef || !GunDef->IsFirearm() || !MagDef->IsMagazine())
		{
			return Log(Who, TEXT("attach"), NAME_None, EAftermathActionResult::NotFound);
		}
		if (!GunDef->AcceptsMagazine(MagDef->Id))
		{
			return Log(Who, TEXT("attach"), MagDef->Id, EAftermathActionResult::NotUsable);
		}

		// Take the new magazine out of the inventory first (frees its cells for the old one).
		FAftermathItemInstance NewMag;
		Inv->RemoveItem(MagUid, NewMag);
		Gun = Inv->FindItemMutable(GunUid); // pointer may move when grids change

		// Old magazine (if any) goes back into the inventory, or on the ground if there's no room.
		if (Gun->HasMagazine())
		{
			if (const FAftermathItemDef* OldDef = DefOf(Gun->AttachedMag))
			{
				FAftermathItemInstance Old = AftermathItems::MakeInstance(*OldDef);
				Old.Loaded = Gun->AttachedMagRounds;
				Old.Health = Gun->AttachedMagHealth;
				Gun->AttachedMag = NAME_None;
				Gun->AttachedMagRounds = 0;
				if (Inv->AddItem(Old) > 0)
				{
					Inv->SpawnPickupNearOwner(Old, nullptr);
				}
				Gun = Inv->FindItemMutable(GunUid);
			}
		}

		Gun->AttachedMag = NewMag.Def;
		Gun->AttachedMagRounds = NewMag.Loaded;
		Gun->AttachedMagHealth = NewMag.Health;
		Inv->MarkChanged();
		return Log(Who, TEXT("attach"), MagDef->Id, EAftermathActionResult::Ok);
	}

	EAftermathActionResult DetachMagazine(AAftermathCharacter& Who, int32 GunUid)
	{
		UAftermathInventoryComponent* Inv = Who.GetInventory();
		if (!Inv || !Who.HasAuthority())
		{
			return EAftermathActionResult::Invalid;
		}
		FAftermathItemInstance* Gun = Inv->FindItemMutable(GunUid);
		const FAftermathItemDef* GunDef = Gun ? DefOf(Gun->Def) : nullptr;
		if (!GunDef || !GunDef->IsFirearm())
		{
			return Log(Who, TEXT("detach"), NAME_None, EAftermathActionResult::NotFound);
		}
		const FAftermathItemDef* MagDef = Gun->HasMagazine() ? DefOf(Gun->AttachedMag) : nullptr;
		if (!MagDef)
		{
			return Log(Who, TEXT("detach"), GunDef->Id, EAftermathActionResult::NothingToTreat);
		}
		FAftermathItemInstance Mag = AftermathItems::MakeInstance(*MagDef);
		Mag.Loaded = Gun->AttachedMagRounds;
		Mag.Health = Gun->AttachedMagHealth;
		if (Inv->AddItem(Mag) > 0)
		{
			return Log(Who, TEXT("detach"), MagDef->Id, EAftermathActionResult::NoRoom);
		}
		Gun = Inv->FindItemMutable(GunUid);
		Gun->AttachedMag = NAME_None;
		Gun->AttachedMagRounds = 0;
		Inv->MarkChanged();
		return Log(Who, TEXT("detach"), MagDef->Id, EAftermathActionResult::Ok);
	}

	EAftermathActionResult Reload(AAftermathCharacter& Who, int32 GunUid)
	{
		UAftermathInventoryComponent* Inv = Who.GetInventory();
		if (!Inv || !Who.HasAuthority())
		{
			return EAftermathActionResult::Invalid;
		}
		const FAftermathItemInstance* Gun = Inv->FindItem(GunUid);
		const FAftermathItemDef* GunDef = Gun ? DefOf(Gun->Def) : nullptr;
		if (!GunDef || !GunDef->IsFirearm())
		{
			return Log(Who, TEXT("reload"), NAME_None, EAftermathActionResult::NotFound);
		}

		// Pick the fullest compatible magazine carried (not the one already in the gun).
		int32 BestUid = 0;
		int32 BestRounds = Gun->HasMagazine() ? Gun->AttachedMagRounds : -1;
		for (const FAftermathEquipSlot& S : Inv->GetSlots())
		{
			for (const FAftermathGridEntry& E : S.Cargo.Entries)
			{
				if (GunDef->AcceptsMagazine(E.Item.Def) && E.Item.Loaded > BestRounds)
				{
					BestRounds = E.Item.Loaded;
					BestUid = E.Item.Uid;
				}
			}
		}
		if (BestUid == 0)
		{
			return Log(Who, TEXT("reload"), GunDef->Id, EAftermathActionResult::NeedsTool); // no better magazine
		}
		return AttachMagazine(Who, GunUid, BestUid);
	}

	EAftermathActionResult Unload(AAftermathCharacter& Who, int32 MagazineUid)
	{
		UAftermathInventoryComponent* Inv = Who.GetInventory();
		if (!Inv || !Who.HasAuthority())
		{
			return EAftermathActionResult::Invalid;
		}
		FAftermathItemInstance* Mag = Inv->FindItemMutable(MagazineUid);
		const FAftermathItemDef* MagDef = Mag ? DefOf(Mag->Def) : nullptr;
		const FAftermathItemDef* AmmoDef = MagDef ? DefOf(MagDef->Magazine.Ammo) : nullptr;
		if (!MagDef || !MagDef->IsMagazine() || !AmmoDef)
		{
			return Log(Who, TEXT("unload"), NAME_None, EAftermathActionResult::NotUsable);
		}
		if (Mag->Loaded <= 0)
		{
			return Log(Who, TEXT("unload"), MagDef->Id, EAftermathActionResult::NothingToTreat);
		}
		const int32 Rounds = Mag->Loaded;
		Mag->Loaded = 0;
		// Rounds may exceed one stack: store them one stack at a time, spill the rest on the ground.
		int32 Left = Rounds;
		while (Left > 0)
		{
			FAftermathItemInstance Part = AftermathItems::MakeInstance(*AmmoDef, FMath::Min(Left, AmmoDef->MaxStack));
			const int32 NotStored = Inv->AddItem(Part);
			if (NotStored > 0)
			{
				Part.Uid = AftermathItems::NextUid();
				Part.Count = NotStored;
				Inv->SpawnPickupNearOwner(Part, nullptr);
			}
			Left -= FMath::Min(Left, AmmoDef->MaxStack);
		}
		Inv->MarkChanged();
		return Log(Who, TEXT("unload"), MagDef->Id, EAftermathActionResult::Ok);
	}
}
