#include "Items/AftermathGrid.h"

#include "Items/AftermathItemDatabase.h"

namespace
{
	bool RectsOverlap(int32 AX, int32 AY, const FAftermathGridSize& A, int32 BX, int32 BY, const FAftermathGridSize& B)
	{
		return AX < BX + B.X && BX < AX + A.X && AY < BY + B.Y && BY < AY + A.Y;
	}
}

FAftermathGridSize FAftermathGrid::Footprint(const FAftermathItemInstance& Item, bool bRotated)
{
	const FAftermathItemDef* Def = DefOf(Item.Def);
	if (!Def)
	{
		return FAftermathGridSize(0, 0);
	}
	return bRotated ? Def->Size.Rotated() : Def->Size;
}

bool FAftermathGrid::CanPlace(const FAftermathItemInstance& Item, int32 X, int32 Y, bool bRotated, int32 IgnoreUid) const
{
	const FAftermathGridSize S = Footprint(Item, bRotated);
	if (!S.IsValid() || X < 0 || Y < 0 || X + S.X > Width || Y + S.Y > Height)
	{
		return false;
	}
	for (const FAftermathGridEntry& E : Entries)
	{
		if (E.Item.Uid == IgnoreUid && IgnoreUid != 0)
		{
			continue;
		}
		if (RectsOverlap(X, Y, S, E.X, E.Y, Footprint(E.Item, E.bRotated)))
		{
			return false;
		}
	}
	return true;
}

bool FAftermathGrid::Place(const FAftermathItemInstance& Item, int32 X, int32 Y, bool bRotated)
{
	if (!Item.IsValid() || FindEntry(Item.Uid) || !CanPlace(Item, X, Y, bRotated))
	{
		return false;
	}
	FAftermathGridEntry& E = Entries.AddDefaulted_GetRef();
	E.Item = Item;
	E.X = X;
	E.Y = Y;
	E.bRotated = bRotated;
	return true;
}

bool FAftermathGrid::FindFreeSpot(const FAftermathItemInstance& Item, int32& OutX, int32& OutY, bool& bOutRotated, int32 IgnoreUid) const
{
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		const bool bRot = Pass == 1;
		const FAftermathGridSize S = Footprint(Item, bRot);
		if (!S.IsValid() || (bRot && S == Footprint(Item, false)))
		{
			continue; // square items: rotated pass is redundant
		}
		for (int32 Y = 0; Y + S.Y <= Height; ++Y)
		{
			for (int32 X = 0; X + S.X <= Width; ++X)
			{
				if (CanPlace(Item, X, Y, bRot, IgnoreUid))
				{
					OutX = X;
					OutY = Y;
					bOutRotated = bRot;
					return true;
				}
			}
		}
	}
	return false;
}

int32 FAftermathGrid::AddAnywhere(const FAftermathItemInstance& Item)
{
	const FAftermathItemDef* Def = DefOf(Item.Def);
	if (!Def || !Item.IsValid() || FindEntry(Item.Uid))
	{
		return Item.Count;
	}

	int32 Remaining = FMath::Max(1, Item.Count);

	// 1) Top up existing stacks (only items identical in everything but count).
	if (Def->IsStackable())
	{
		for (FAftermathGridEntry& E : Entries)
		{
			if (Remaining <= 0)
			{
				break;
			}
			if (E.Item.Def == Item.Def && E.Item.Count < Def->MaxStack && FMath::IsNearlyEqual(E.Item.Health, Item.Health, 0.05f))
			{
				const int32 Move = FMath::Min(Remaining, Def->MaxStack - E.Item.Count);
				E.Item.Count += Move;
				Remaining -= Move;
			}
		}
	}

	// 2) Place the rest as one new entry.
	if (Remaining > 0)
	{
		FAftermathItemInstance Rest = Item;
		Rest.Count = Remaining;
		int32 X, Y;
		bool bRot;
		if (FindFreeSpot(Rest, X, Y, bRot) && Place(Rest, X, Y, bRot))
		{
			Remaining = 0;
		}
	}
	return Remaining;
}

bool FAftermathGrid::MoveWithin(int32 Uid, int32 X, int32 Y, bool bRotated)
{
	FAftermathGridEntry* E = FindEntry(Uid);
	if (!E || !CanPlace(E->Item, X, Y, bRotated, Uid))
	{
		return false;
	}
	E->X = X;
	E->Y = Y;
	E->bRotated = bRotated;
	return true;
}

bool FAftermathGrid::Remove(int32 Uid, FAftermathItemInstance& OutItem)
{
	const int32 Index = Entries.IndexOfByPredicate([Uid](const FAftermathGridEntry& E) { return E.Item.Uid == Uid; });
	if (Index == INDEX_NONE)
	{
		return false;
	}
	OutItem = Entries[Index].Item;
	Entries.RemoveAt(Index);
	return true;
}

bool FAftermathGrid::Split(int32 Uid, int32 Amount, int32 NewUid)
{
	FAftermathGridEntry* E = FindEntry(Uid);
	if (!E || NewUid == 0 || FindEntry(NewUid) || Amount <= 0 || Amount >= E->Item.Count)
	{
		return false;
	}

	FAftermathItemInstance Part = E->Item;
	Part.Uid = NewUid;
	Part.Count = Amount;

	int32 X, Y;
	bool bRot;
	if (!FindFreeSpot(Part, X, Y, bRot))
	{
		return false;
	}
	// Commit: shrink the source first (the pointer is invalidated by Place's array growth).
	E->Item.Count -= Amount;
	return Place(Part, X, Y, bRot);
}

int32 FAftermathGrid::MergeInto(int32 SourceUid, int32 TargetUid)
{
	if (SourceUid == TargetUid)
	{
		return 0;
	}
	FAftermathGridEntry* Src = FindEntry(SourceUid);
	FAftermathGridEntry* Dst = FindEntry(TargetUid);
	const FAftermathItemDef* Def = Src ? DefOf(Src->Item.Def) : nullptr;
	if (!Src || !Dst || !Def || !Def->IsStackable() || Src->Item.Def != Dst->Item.Def)
	{
		return 0;
	}

	const int32 Move = FMath::Min(Src->Item.Count, Def->MaxStack - Dst->Item.Count);
	if (Move <= 0)
	{
		return 0;
	}
	Dst->Item.Count += Move;
	Src->Item.Count -= Move;
	if (Src->Item.Count <= 0)
	{
		FAftermathItemInstance Dummy;
		Remove(SourceUid, Dummy);
	}
	return Move;
}

FAftermathGridEntry* FAftermathGrid::FindEntry(int32 Uid)
{
	return Uid == 0 ? nullptr : Entries.FindByPredicate([Uid](const FAftermathGridEntry& E) { return E.Item.Uid == Uid; });
}

const FAftermathGridEntry* FAftermathGrid::FindEntry(int32 Uid) const
{
	return Uid == 0 ? nullptr : Entries.FindByPredicate([Uid](const FAftermathGridEntry& E) { return E.Item.Uid == Uid; });
}

int32 FAftermathGrid::UidAt(int32 X, int32 Y) const
{
	for (const FAftermathGridEntry& E : Entries)
	{
		const FAftermathGridSize S = Footprint(E.Item, E.bRotated);
		if (X >= E.X && X < E.X + S.X && Y >= E.Y && Y < E.Y + S.Y)
		{
			return E.Item.Uid;
		}
	}
	return 0;
}

int32 FAftermathGrid::CountOf(FName Def) const
{
	int32 N = 0;
	for (const FAftermathGridEntry& E : Entries)
	{
		if (E.Item.Def == Def)
		{
			N += E.Item.Count;
		}
	}
	return N;
}

int32 FAftermathGrid::TotalWeight() const
{
	int32 Grams = 0;
	for (const FAftermathGridEntry& E : Entries)
	{
		Grams += AftermathItems::InstanceWeight(E.Item);
	}
	return Grams;
}
