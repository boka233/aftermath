#pragma once

#include "CoreMinimal.h"
#include "Items/AftermathItemInstance.h"
#include "AftermathGrid.generated.h"

/** An item placed in a grid. X,Y is the top-left cell. */
USTRUCT(BlueprintType)
struct AFTERMATH_API FAftermathGridEntry
{
	GENERATED_BODY()

	UPROPERTY()
	FAftermathItemInstance Item;

	UPROPERTY()
	int32 X = 0;

	UPROPERTY()
	int32 Y = 0;

	/** Rotated 90 degrees: footprint is Size.Y x Size.X. */
	UPROPERTY()
	bool bRotated = false;
};

/**
 * Pure grid container logic (no actors, no networking): placement, overlap, stacking, splitting.
 * Every mutation is "check then commit", so a failed call leaves the grid unchanged.
 * Item sizes come from UAftermathItemDatabase.
 */
USTRUCT(BlueprintType)
struct AFTERMATH_API FAftermathGrid
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Width = 0;

	UPROPERTY()
	int32 Height = 0;

	UPROPERTY()
	TArray<FAftermathGridEntry> Entries;

	FAftermathGrid() = default;
	FAftermathGrid(int32 InW, int32 InH) : Width(InW), Height(InH) {}

	bool IsValid() const { return Width > 0 && Height > 0; }
	bool IsEmpty() const { return Entries.Num() == 0; }

	/** Footprint of an item in this orientation. Zero size if the def is unknown. */
	static FAftermathGridSize Footprint(const FAftermathItemInstance& Item, bool bRotated);

	/** True if the rectangle fits inside bounds and overlaps no entry (optionally ignoring one uid). */
	bool CanPlace(const FAftermathItemInstance& Item, int32 X, int32 Y, bool bRotated, int32 IgnoreUid = 0) const;

	/** Place at a specific spot. */
	bool Place(const FAftermathItemInstance& Item, int32 X, int32 Y, bool bRotated);

	/** First free spot scanning row-major, trying unrotated then rotated. */
	bool FindFreeSpot(const FAftermathItemInstance& Item, int32& OutX, int32& OutY, bool& bOutRotated, int32 IgnoreUid = 0) const;

	/**
	 * Add anywhere: first merge into existing stacks of the same def, then place the rest in a free spot.
	 * Returns the amount that could NOT be stored (0 = everything stored). Item.Count is the amount to add.
	 * A partially stored stack keeps the original uid on the placed remainder only when it didn't merge fully.
	 */
	int32 AddAnywhere(const FAftermathItemInstance& Item);

	/** Move an entry within this grid. */
	bool MoveWithin(int32 Uid, int32 X, int32 Y, bool bRotated);

	/** Remove and return. False if not found. */
	bool Remove(int32 Uid, FAftermathItemInstance& OutItem);

	/** Split Amount off a stack into a new entry at a free spot. The new entry gets NewUid. */
	bool Split(int32 Uid, int32 Amount, int32 NewUid);

	/** Merge Source into the stack Target (same def). Moves as many as fit. Returns amount moved. */
	int32 MergeInto(int32 SourceUid, int32 TargetUid);

	FAftermathGridEntry* FindEntry(int32 Uid);
	const FAftermathGridEntry* FindEntry(int32 Uid) const;

	/** Uid of the entry covering a cell, 0 if empty. */
	int32 UidAt(int32 X, int32 Y) const;

	int32 CountOf(FName Def) const;
	int32 TotalWeight() const;
};
