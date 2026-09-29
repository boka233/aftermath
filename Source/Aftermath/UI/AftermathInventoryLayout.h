#pragma once

#include "CoreMinimal.h"
#include "Items/AftermathItemTypes.h"

class UAftermathInventoryComponent;
class AAftermathPickup;

/**
 * Screen layout of the inventory panel, computed from the inventory state and the viewport size.
 * Pure geometry: the HUD draws it and the controller hit-tests mouse clicks against it, so both
 * always agree on where each cell is. No engine drawing calls here (unit-testable).
 *
 *   [ Nearby (ground) ]   [ Torso ][ Legs ][ Feet ][ Back ]      <- equip slots row
 *                          [ torso cargo grid ]
 *                          [ legs cargo grid  ]
 *                          [ back cargo grid  ]
 */
struct FAftermathInvRect
{
	float X = 0.f, Y = 0.f, W = 0.f, H = 0.f;
	bool Contains(float PX, float PY) const { return PX >= X && PX < X + W && PY >= Y && PY < Y + H; }
};

struct FAftermathInvGridView
{
	EAftermathWearSlot Container = EAftermathWearSlot::None;
	FAftermathInvRect Rect;
	int32 Cols = 0;
	int32 Rows = 0;

	/** Cell under a screen point; false if outside. */
	bool CellAt(float PX, float PY, float Cell, int32& OutX, int32& OutY) const
	{
		if (!Rect.Contains(PX, PY))
		{
			return false;
		}
		OutX = FMath::Clamp(FMath::FloorToInt((PX - Rect.X) / Cell), 0, Cols - 1);
		OutY = FMath::Clamp(FMath::FloorToInt((PY - Rect.Y) / Cell), 0, Rows - 1);
		return true;
	}
};

struct FAftermathInvSlotView
{
	EAftermathWearSlot Slot = EAftermathWearSlot::None;
	FAftermathInvRect Rect;
};

struct FAftermathInvNearbyRow
{
	TWeakObjectPtr<AAftermathPickup> Pickup;
	FAftermathInvRect Rect;
};

/** What a screen point hits. */
struct FAftermathInvHit
{
	enum class EKind : uint8 { None, GridCell, EquipSlot, Nearby, Hotbar };
	EKind Kind = EKind::None;
	EAftermathWearSlot Container = EAftermathWearSlot::None;
	int32 CellX = 0;
	int32 CellY = 0;
	/** Item under the point (grid entry, worn item, or hotbar item), 0 if empty. */
	int32 Uid = 0;
	TWeakObjectPtr<AAftermathPickup> Pickup;
	int32 HotbarIndex = -1;
};

struct AFTERMATH_API FAftermathInventoryLayout
{
	float Cell = 40.f;
	FAftermathInvRect Panel;
	FAftermathInvRect NearbyPanel;
	TArray<FAftermathInvSlotView> Slots;
	TArray<FAftermathInvGridView> Grids;
	TArray<FAftermathInvNearbyRow> Nearby;
	/** Always visible (also when the panel is closed). */
	TArray<FAftermathInvRect> Hotbar;

	static constexpr int32 HotbarSize = 9;

	/** Build for a viewport. Nearby pickups are listed in the order given. */
	void Build(float ViewW, float ViewH, const UAftermathInventoryComponent& Inv, const TArray<AAftermathPickup*>& NearbyPickups, bool bPanelOpen);

	/** Screen rect of an item in a grid (for drawing). */
	FAftermathInvRect ItemRect(const FAftermathInvGridView& Grid, int32 X, int32 Y, const FAftermathGridSize& Footprint) const;

	const FAftermathInvGridView* FindGrid(EAftermathWearSlot Container) const;

	FAftermathInvHit HitTest(float PX, float PY, const UAftermathInventoryComponent& Inv, const TArray<int32>& HotbarUids) const;
};
