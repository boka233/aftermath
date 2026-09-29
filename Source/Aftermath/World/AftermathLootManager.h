#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "World/AftermathLootEconomy.h"
#include "AftermathLootManager.generated.h"

class AAftermathLootSpawnPoint;
class AAftermathPickup;

/**
 * Server-only loot economy for a world. Loads Content/Data/loot.json; every RestockIntervalSeconds it
 * counts what lies on the ground, cleans up economy items that outlived their Lifetime (never in front of
 * a player), and asks AftermathLoot::PlanRestock where to put new ones. Only active in worlds that have
 * loot spawn points (the World map), so the small test map keeps its hand-placed loot.
 */
UCLASS()
class AFTERMATH_API UAftermathLootManager : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	void RegisterPoint(AAftermathLootSpawnPoint* Point);
	void UnregisterPoint(AAftermathLootSpawnPoint* Point);

	/** Run one cleanup + restock pass now (tests; also runs on a timer). Returns items spawned. */
	int32 RunPass();

	/** Replace the loaded config (tests). */
	void SetConfig(const AftermathLoot::FConfig& InConfig) { Config = InConfig; bConfigLoaded = true; }
	const AftermathLoot::FConfig& GetConfig() const { return Config; }

	int32 NumPoints() const { return Points.Num(); }
	/** Economy items currently on the ground (spawned by us, not yet taken). */
	int32 NumTracked() const { return Tracked.Num(); }
	int32 LastSpawned() const { return LastSpawnedCount; }
	int32 LastCleaned() const { return LastCleanedCount; }

private:
	bool LoadConfig();

	struct FTrackedItem
	{
		TWeakObjectPtr<AAftermathPickup> Pickup;
		int32 PointIndex = INDEX_NONE;
		FName ItemId;
		double SpawnedAt = 0.0;
	};

	AftermathLoot::FConfig Config;
	bool bConfigLoaded = false;
	TArray<TWeakObjectPtr<AAftermathLootSpawnPoint>> Points;
	TArray<FTrackedItem> Tracked;
	float Timer = 0.f;
	int32 PassCount = 0;
	int32 LastSpawnedCount = 0;
	int32 LastCleanedCount = 0;
};
