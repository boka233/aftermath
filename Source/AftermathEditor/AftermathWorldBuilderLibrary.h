#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "AftermathWorldBuilderLibrary.generated.h"

class ALandscape;
class UMaterialInterface;

/** One generated settlement, for the Python map builder to place buildings/loot around. */
USTRUCT(BlueprintType)
struct FAftermathWorldSite
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Aftermath") FString Kind;
	UPROPERTY(BlueprintReadOnly, Category = "Aftermath") FString Name;
	UPROPERTY(BlueprintReadOnly, Category = "Aftermath") FVector Center = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category = "Aftermath") float Radius = 0.f;
};

/** One road polyline (points on the ground). */
USTRUCT(BlueprintType)
struct FAftermathWorldRoad
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Aftermath") TArray<FVector> Points;
};

/** One coastal spawn. */
USTRUCT(BlueprintType)
struct FAftermathWorldSpawn
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Aftermath") FVector Location = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category = "Aftermath") float Yaw = 0.f;
};

/**
 * Editor-only helpers called from Tools/build_world_map.py. The terrain rules live in the runtime module
 * (AftermathTerrainGen, unit-tested); this library turns them into a Landscape and exposes the layout.
 */
UCLASS()
class UAftermathWorldBuilderLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Build the 10 x 10 km landscape in the current editor world: generates the heightmap (Verts x Verts),
	 * spawns an ALandscape centred on the origin, imports the heights and splits it into World Partition
	 * streaming proxies (GridSizeInComponents per proxy). Returns the landscape, or null on failure.
	 */
	UFUNCTION(BlueprintCallable, Category = "Aftermath|WorldBuilder")
	static ALandscape* CreateWorldLandscape(int32 Seed, int32 Verts, int32 QuadsPerSection, int32 SectionsPerComponent,
		float ZScale, int32 GridSizeInComponents, UMaterialInterface* Material);

	/** Ground height (cm) at XY from the same rules the landscape was built from. */
	UFUNCTION(BlueprintCallable, Category = "Aftermath|WorldBuilder")
	static float GetWorldHeight(int32 Seed, float X, float Y);

	UFUNCTION(BlueprintCallable, Category = "Aftermath|WorldBuilder")
	static TArray<FAftermathWorldSite> GetWorldSites(int32 Seed);

	UFUNCTION(BlueprintCallable, Category = "Aftermath|WorldBuilder")
	static TArray<FAftermathWorldRoad> GetWorldRoads(int32 Seed);

	UFUNCTION(BlueprintCallable, Category = "Aftermath|WorldBuilder")
	static TArray<FAftermathWorldSpawn> GetWorldSpawns(int32 Seed);

	/** Size on disk (bytes) of every file under a content folder, e.g. "/Game/Maps" (for the build report). */
	UFUNCTION(BlueprintCallable, Category = "Aftermath|WorldBuilder")
	static int64 GetContentFolderBytes(const FString& ContentPath);

	/**
	 * Navigation diagnostic: loads the world partition cells around Center (editor world), builds the
	 * navmesh there, and logs what the navigation octree holds in that box (per class: element count,
	 * how many carry geometry, pending lazy gathering, vertex/face counts) plus the resulting tile/poly
	 * count and whether Center projects onto the navmesh. Returns a one-line summary.
	 */
	UFUNCTION(BlueprintCallable, Category = "Aftermath|WorldBuilder")
	static FString DiagnoseNavigationAt(FVector Center, float HalfSize);
};
