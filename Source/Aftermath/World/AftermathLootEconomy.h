#pragma once

#include "CoreMinimal.h"

/**
 * Pure loot economy rules (no world access, unit-testable). DayZ-style "central economy", simplified:
 *  - every item has a target amount on the ground across the whole map (Nominal) and a hard cap (Max);
 *  - spawn points are tagged by the building they sit in (House, Medical, Police, Military, Farm...);
 *  - a restock pass picks up to Batch items that are furthest below Nominal and places each at a free
 *    point with a matching tag that no player is near;
 *  - items untouched on the ground longer than their Lifetime are cleaned up (and restocked elsewhere).
 * The world-facing code (UAftermathLootManager) supplies counts, points and player positions.
 */
namespace AftermathLoot
{
	struct FItemRule
	{
		FName Id;
		int32 Nominal = 0;
		int32 Max = 0;
		int32 Count = 1;
		TArray<FName> Tags;
		float Lifetime = 3600.f;
	};

	struct FConfig
	{
		float RestockIntervalSeconds = 60.f;
		int32 RestockBatch = 40;
		/** Never spawn in sight of a player: points closer than this (cm) to any player are skipped. */
		float MinPlayerDistance = 5000.f;
		TArray<FItemRule> Items;

		const FItemRule* Find(FName Id) const;
	};

	/** Parse loot.json. False (and OutError) on bad JSON, unknown fields types, Max < Nominal, empty tags. */
	AFTERMATH_API bool ParseConfig(const FString& Json, FConfig& Out, FString& OutError);

	struct FSpawnPoint
	{
		FVector Location = FVector::ZeroVector;
		FName Tag;
		/** An item currently lies here (placed by the economy and not yet taken). */
		bool bOccupied = false;
	};

	struct FPlacement
	{
		FName ItemId;
		int32 Count = 1;
		int32 PointIndex = INDEX_NONE;
	};

	/**
	 * One restock pass. OnGround[i] = how many of Config.Items[i] lie on the ground now (placed by the
	 * economy or dropped by players). Returns up to Config.RestockBatch placements, never exceeding Max,
	 * never using an occupied point or one near a player, each point used at most once per pass.
	 * Items furthest below their Nominal (relative) go first. Seed makes the point choice deterministic.
	 */
	AFTERMATH_API TArray<FPlacement> PlanRestock(const FConfig& Config, const TArray<int32>& OnGround,
		const TArray<FSpawnPoint>& Points, const TArray<FVector>& Players, int32 Seed);

	/** Cleanup rule: an untouched economy item older than its lifetime goes. */
	AFTERMATH_API bool ShouldCleanup(const FItemRule& Rule, double SpawnedAt, double Now, bool bNearPlayer);
}
