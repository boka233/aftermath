#include "UI/AftermathInventoryLayout.h"

#include "Interaction/AftermathPickup.h"
#include "Items/AftermathInventoryComponent.h"

namespace
{
	// Seven equip slots in one row (hands first: it's what you use most).
	constexpr EAftermathWearSlot SlotOrder[] = {
		EAftermathWearSlot::Hands, EAftermathWearSlot::Head, EAftermathWearSlot::Torso, EAftermathWearSlot::Vest,
		EAftermathWearSlot::Legs, EAftermathWearSlot::Feet, EAftermathWearSlot::Back };
	constexpr EAftermathWearSlot GridOrder[] = {
		EAftermathWearSlot::Torso, EAftermathWearSlot::Vest, EAftermathWearSlot::Legs, EAftermathWearSlot::Back };
	constexpr int32 NumSlots = UE_ARRAY_COUNT(SlotOrder);
	/** Equip slots are drawn this many cells wide/tall. */
	constexpr float SlotCells = 1.6f;

	constexpr float LayoutGap = 12.f;
	constexpr float LayoutHeader = 22.f;
}

void FAftermathInventoryLayout::Build(float ViewW, float ViewH, const UAftermathInventoryComponent& Inv,
	const TArray<AAftermathPickup*>& NearbyPickups, bool bPanelOpen)
{
	Slots.Reset();
	Grids.Reset();
	Nearby.Reset();
	Hotbar.Reset();

	// Cells scale with resolution: 40px at 1080p.
	Cell = FMath::Clamp(FMath::RoundToFloat(ViewH / 27.f), 24.f, 64.f);

	// The right column must fit between the help text (top) and the hotbar (bottom). Its height is
	// fixed rows (headers, gaps) plus a number of cell rows; shrink the cell until it fits.
	const float TopY = FMath::Max(ViewH * 0.08f, 80.f);
	if (bPanelOpen)
	{
		int32 CellRows = 2; // equip slots are ~2 cells tall
		int32 Sections = 1;
		for (EAftermathWearSlot C : GridOrder)
		{
			if (const FAftermathGrid* G = Inv.GetGrid(C))
			{
				CellRows += G->Height;
				++Sections;
			}
		}
		const float Fixed = Sections * (LayoutHeader + LayoutGap);
		// Bottom limit: above the hotbar (cell + 16px margin + 20px for its number labels).
		for (int32 Guard = 0; Guard < 64; ++Guard)
		{
			const float Bottom = ViewH - Cell - 36.f;
			if (TopY + Fixed + CellRows * Cell <= Bottom || Cell <= 8.f)
			{
				break;
			}
			Cell -= 1.f;
		}
	}

	// Hotbar: bottom centre.
	const float HotW = HotbarSize * Cell + (HotbarSize - 1) * 4.f;
	for (int32 i = 0; i < HotbarSize; ++i)
	{
		FAftermathInvRect R;
		R.X = (ViewW - HotW) * 0.5f + i * (Cell + 4.f);
		R.Y = ViewH - Cell - 16.f;
		R.W = Cell;
		R.H = Cell;
		Hotbar.Add(R);
	}

	if (!bPanelOpen)
	{
		return;
	}

	// Right column: slots row, then each cargo grid stacked.
	const float SlotSize = Cell * SlotCells;
	const float ColumnW = FMath::Max(NumSlots * (SlotSize + 6.f), 6.f * Cell + 8.f);
	// Keep the column on screen: start at the middle, shift left if the 7 slots would overflow.
	const float ColumnX = FMath::Min(ViewW * 0.5f - 10.f, ViewW - ColumnW - 30.f);
	// Leave room for the two help-text lines at the top of the screen (TopY computed above).
	float Y = TopY;

	Panel = { ColumnX - 10.f, Y - 10.f, ColumnW + 20.f, 0.f };

	for (int32 i = 0; i < UE_ARRAY_COUNT(SlotOrder); ++i)
	{
		FAftermathInvSlotView& S = Slots.AddDefaulted_GetRef();
		S.Slot = SlotOrder[i];
		S.Rect = { ColumnX + i * (SlotSize + 6.f), Y + LayoutHeader, SlotSize, SlotSize };
	}
	Y += LayoutHeader + SlotSize + LayoutGap;

	for (EAftermathWearSlot C : GridOrder)
	{
		const FAftermathGrid* G = Inv.GetGrid(C);
		if (!G)
		{
			continue;
		}
		FAftermathInvGridView& V = Grids.AddDefaulted_GetRef();
		V.Container = C;
		V.Cols = G->Width;
		V.Rows = G->Height;
		V.Rect = { ColumnX, Y + LayoutHeader, G->Width * Cell, G->Height * Cell };
		Y += LayoutHeader + G->Height * Cell + LayoutGap;
	}
	Panel.H = Y - Panel.Y;

	// Left column: nearby ground items.
	// Nearby column fills the space left of the gear column (never wider than 22% of the screen).
	const float NearW = FMath::Min(ViewW * 0.22f, FMath::Max(60.f, ColumnX - LayoutGap * 2.f - 20.f));
	NearbyPanel = { ColumnX - LayoutGap * 2.f - NearW, TopY - 10.f, NearW, 0.f };
	float NY = NearbyPanel.Y + LayoutHeader + 10.f;
	for (AAftermathPickup* P : NearbyPickups)
	{
		if (!P)
		{
			continue;
		}
		FAftermathInvNearbyRow& Row = Nearby.AddDefaulted_GetRef();
		Row.Pickup = P;
		Row.Rect = { NearbyPanel.X + 6.f, NY, NearW - 12.f, Cell * 0.8f };
		NY += Cell * 0.8f + 4.f;
	}
	NearbyPanel.H = FMath::Max(NY - NearbyPanel.Y + 6.f, Cell * 3.f);
}

FAftermathInvRect FAftermathInventoryLayout::ItemRect(const FAftermathInvGridView& Grid, int32 X, int32 Y, const FAftermathGridSize& Footprint) const
{
	return { Grid.Rect.X + X * Cell + 1.f, Grid.Rect.Y + Y * Cell + 1.f, Footprint.X * Cell - 2.f, Footprint.Y * Cell - 2.f };
}

const FAftermathInvGridView* FAftermathInventoryLayout::FindGrid(EAftermathWearSlot Container) const
{
	return Grids.FindByPredicate([Container](const FAftermathInvGridView& G) { return G.Container == Container; });
}

FAftermathInvHit FAftermathInventoryLayout::HitTest(float PX, float PY, const UAftermathInventoryComponent& Inv, const TArray<int32>& HotbarUids) const
{
	FAftermathInvHit Hit;

	for (int32 i = 0; i < Hotbar.Num(); ++i)
	{
		if (Hotbar[i].Contains(PX, PY))
		{
			Hit.Kind = FAftermathInvHit::EKind::Hotbar;
			Hit.HotbarIndex = i;
			Hit.Uid = HotbarUids.IsValidIndex(i) ? HotbarUids[i] : 0;
			return Hit;
		}
	}
	for (const FAftermathInvGridView& G : Grids)
	{
		int32 CX, CY;
		if (G.CellAt(PX, PY, Cell, CX, CY))
		{
			Hit.Kind = FAftermathInvHit::EKind::GridCell;
			Hit.Container = G.Container;
			Hit.CellX = CX;
			Hit.CellY = CY;
			const FAftermathGrid* Grid = Inv.GetGrid(G.Container);
			Hit.Uid = Grid ? Grid->UidAt(CX, CY) : 0;
			return Hit;
		}
	}
	for (const FAftermathInvSlotView& S : Slots)
	{
		if (S.Rect.Contains(PX, PY))
		{
			Hit.Kind = FAftermathInvHit::EKind::EquipSlot;
			Hit.Container = S.Slot;
			const FAftermathEquipSlot* E = Inv.GetSlot(S.Slot);
			Hit.Uid = (E && !E->IsEmpty()) ? E->Item.Uid : 0;
			return Hit;
		}
	}
	for (const FAftermathInvNearbyRow& Row : Nearby)
	{
		if (Row.Rect.Contains(PX, PY))
		{
			Hit.Kind = FAftermathInvHit::EKind::Nearby;
			Hit.Pickup = Row.Pickup;
			return Hit;
		}
	}
	return Hit;
}
