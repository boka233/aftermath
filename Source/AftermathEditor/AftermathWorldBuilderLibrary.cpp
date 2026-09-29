#include "AftermathWorldBuilderLibrary.h"

#include "Editor.h"
#include "Misc/App.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "AI/Navigation/NavDataGatheringMode.h"
#include "Landscape.h"
#include "LandscapeInfo.h"
#include "LandscapeProxy.h"
#include "LandscapeSubsystem.h"
#include "LandscapeComponent.h"
#include "LandscapeHeightfieldCollisionComponent.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "NavigationOctree.h"
#include "NavigationSystem.h"
#include "NavMesh/RecastHelpers.h"
#include "NavMesh/RecastNavMesh.h"
#include "NavMesh/RecastNavMeshGenerator.h"
#include "UObject/UObjectIterator.h"
#include "WorldPartition/LoaderAdapter/LoaderAdapterShape.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionEditorLoaderAdapter.h"
#include "World/AftermathTerrainGen.h"

DEFINE_LOG_CATEGORY_STATIC(LogAftermathWorldBuilder, Log, All);

namespace
{
	/** The terrain is deterministic from the seed; keep the last one around (layout + heights are reused). */
	const AftermathTerrainGen::FTerrain& TerrainFor(int32 Seed)
	{
		static TUniquePtr<AftermathTerrainGen::FTerrain> Cached;
		static int32 CachedSeed = TNumericLimits<int32>::Min();
		if (!Cached || CachedSeed != Seed)
		{
			AftermathTerrainGen::FSettings S;
			S.Seed = Seed;
			Cached = MakeUnique<AftermathTerrainGen::FTerrain>(S);
			CachedSeed = Seed;
		}
		return *Cached;
	}

	FVector OnGround(int32 Seed, const FVector2D& P, float ExtraZ = 0.f)
	{
		return FVector(P.X, P.Y, TerrainFor(Seed).HeightAt(P) + ExtraZ);
	}
}

ALandscape* UAftermathWorldBuilderLibrary::CreateWorldLandscape(int32 Seed, int32 Verts, int32 QuadsPerSection, int32 SectionsPerComponent,
	float ZScale, int32 GridSizeInComponents, UMaterialInterface* Material)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		UE_LOG(LogAftermathWorldBuilder, Error, TEXT("CreateWorldLandscape: no editor world"));
		return nullptr;
	}
	const int32 QuadsPerComponent = QuadsPerSection * SectionsPerComponent;
	if (QuadsPerComponent <= 0 || (Verts - 1) % QuadsPerComponent != 0)
	{
		UE_LOG(LogAftermathWorldBuilder, Error, TEXT("CreateWorldLandscape: Verts-1 (%d) must be a multiple of the component size (%d quads)"),
			Verts - 1, QuadsPerComponent);
		return nullptr;
	}

	const AftermathTerrainGen::FTerrain& Terrain = TerrainFor(Seed);
	const float SizeCm = Terrain.GetSettings().SizeCm;
	const float XYScale = SizeCm / static_cast<float>(Verts - 1);

	const double T0 = FPlatformTime::Seconds();
	TArray<uint16> Heights;
	Terrain.FillHeightmap(Verts, ZScale, Heights);
	UE_LOG(LogAftermathWorldBuilder, Display, TEXT("Heightmap %dx%d generated in %.1fs"), Verts, Verts, FPlatformTime::Seconds() - T0);

	// Landscape origin is its (0,0) vertex; put it at the south-west corner so the map is centred on the origin.
	const FVector Location(-SizeCm * 0.5f, -SizeCm * 0.5f, 0.f);
	ALandscape* Landscape = World->SpawnActor<ALandscape>(Location, FRotator::ZeroRotator);
	if (!Landscape)
	{
		UE_LOG(LogAftermathWorldBuilder, Error, TEXT("CreateWorldLandscape: spawn failed"));
		return nullptr;
	}
	Landscape->SetActorRelativeScale3D(FVector(XYScale, XYScale, ZScale));
	if (Material)
	{
		Landscape->LandscapeMaterial = Material;
	}
	Landscape->StaticLightingLOD = FMath::DivideAndRoundUp(FMath::CeilLogTwo((Verts * Verts) / (2048 * 2048) + 1), static_cast<uint32>(2));

	TMap<FGuid, TArray<uint16>> HeightData;
	HeightData.Add(FGuid(), MoveTemp(Heights));
	TMap<FGuid, TArray<FLandscapeImportLayerInfo>> LayerData;
	LayerData.Add(FGuid(), TArray<FLandscapeImportLayerInfo>());

	const double T1 = FPlatformTime::Seconds();
	Landscape->Import(FGuid::NewGuid(), 0, 0, Verts - 1, Verts - 1, SectionsPerComponent, QuadsPerSection, HeightData,
		nullptr, LayerData, ELandscapeImportAlphamapType::Additive, TArrayView<const FLandscapeLayer>());
	Landscape->SetActorLabel(TEXT("WorldLandscape"));

	ULandscapeInfo* Info = Landscape->GetLandscapeInfo();
	if (!Info)
	{
		UE_LOG(LogAftermathWorldBuilder, Error, TEXT("CreateWorldLandscape: import produced no LandscapeInfo"));
		return nullptr;
	}
	Info->UpdateLayerInfoMap(Landscape);
	UE_LOG(LogAftermathWorldBuilder, Display, TEXT("Landscape imported in %.1fs"), FPlatformTime::Seconds() - T1);

	// Navigation: export the landscape's full heightfield per tile (Instant) instead of the lazy
	// cached-sample slices that invoker-based generation defaults to. Streaming proxies copy this
	// "LandscapeOverridable" setting from the parent when they're created below.
	Landscape->bUsedForNavigation = true;
	Landscape->NavigationGeometryGatheringMode = ENavDataGatheringMode::Instant;

	// Edit layers: Import() writes the heights into the edit layer; the final heightmap textures (what's
	// rendered and what component bounds are computed from) are produced by a GPU merge. Force it now.
	// Needs a real RHI: run the builder with -RenderOffscreen, not -nullrhi, or the terrain stays flat.
	if (!FApp::CanEverRender())
	{
		UE_LOG(LogAftermathWorldBuilder, Error, TEXT("CreateWorldLandscape: no renderer (-nullrhi?) - the landscape heightmap can't be merged; run with -RenderOffscreen"));
	}
	Landscape->ForceUpdateLayersContent();

	// Split into streaming proxies so World Partition can stream the terrain in cells.
	if (ULandscapeSubsystem* LS = World->GetSubsystem<ULandscapeSubsystem>())
	{
		const double T2 = FPlatformTime::Seconds();
		LS->ChangeGridSize(Info, FMath::Clamp(GridSizeInComponents, 1, 16));
		UE_LOG(LogAftermathWorldBuilder, Display, TEXT("Landscape split into streaming proxies (grid %d) in %.1fs"),
			GridSizeInComponents, FPlatformTime::Seconds() - T2);
	}

	// Streaming proxies carry their own copy of the navigation settings: set them on every proxy explicitly.
	// Also recompute each component's bounds from the imported heights. After Import() they are flat
	// placeholders (Z ~ -257 m here); the collision component copies them, and navigation only gathers
	// geometry whose bounds overlap a tile - so with stale bounds the terrain never reached the navmesh.
	int32 Proxies = 0;
	int32 ProxiesFixed = 0;
	int32 BoundsUpdated = 0;
	Info->ForEachLandscapeProxy([&Proxies, &ProxiesFixed, &BoundsUpdated](ALandscapeProxy* Proxy)
	{
		for (ULandscapeComponent* Comp : Proxy->LandscapeComponents)
		{
			if (Comp && Comp->UpdateCachedBounds(/*bInApproximateBounds=*/ false))
			{
				++BoundsUpdated;
			}
			if (Comp)
			{
				Comp->UpdateComponentToWorld();
				if (ULandscapeHeightfieldCollisionComponent* Col = Comp->GetCollisionComponent())
				{
					Col->UpdateComponentToWorld();
				}
			}
		}
		++Proxies;
		if (!Proxy->bUsedForNavigation || Proxy->NavigationGeometryGatheringMode != ENavDataGatheringMode::Instant)
		{
			++ProxiesFixed;
		}
		Proxy->bUsedForNavigation = true;
		Proxy->NavigationGeometryGatheringMode = ENavDataGatheringMode::Instant;
		Proxy->MarkPackageDirty();
		return true;
	});
	UE_LOG(LogAftermathWorldBuilder, Display, TEXT("Landscape proxies with navigation settings fixed: %d of %d; component bounds recomputed: %d"),
		ProxiesFixed, Proxies, BoundsUpdated);

	// Sanity check: the rendered heightmap must match the generated terrain (fails when the merge didn't run).
	double MinZ = TNumericLimits<double>::Max(), MaxZ = TNumericLimits<double>::Lowest();
	Info->ForEachLandscapeProxy([&MinZ, &MaxZ](ALandscapeProxy* Proxy)
	{
		for (ULandscapeComponent* Comp : Proxy->LandscapeComponents)
		{
			if (Comp)
			{
				const FBox B = Comp->Bounds.GetBox();
				MinZ = FMath::Min(MinZ, B.Min.Z);
				MaxZ = FMath::Max(MaxZ, B.Max.Z);
			}
		}
		return true;
	});
	UE_LOG(LogAftermathWorldBuilder, Display, TEXT("[Aftermath] WorldLandscapeBounds min_z=%.0f max_z=%.0f (expected about %.0f..%.0f)"),
		MinZ, MaxZ, -Terrain.GetSettings().SeaDepthCm, Terrain.GetSettings().MaxHeightCm);
	UE_LOG(LogAftermathWorldBuilder, Display, TEXT("[Aftermath] WorldLandscape verts=%d quads_per_component=%d xy_scale=%.1f z_scale=%.1f proxies=%d"),
		Verts, QuadsPerComponent, XYScale, ZScale, Proxies);
	return Landscape;
}

float UAftermathWorldBuilderLibrary::GetWorldHeight(int32 Seed, float X, float Y)
{
	return TerrainFor(Seed).HeightAt(FVector2D(X, Y));
}

TArray<FAftermathWorldSite> UAftermathWorldBuilderLibrary::GetWorldSites(int32 Seed)
{
	TArray<FAftermathWorldSite> Out;
	for (const AftermathTerrainGen::FSite& S : TerrainFor(Seed).GetLayout().Sites)
	{
		FAftermathWorldSite& W = Out.AddDefaulted_GetRef();
		W.Kind = AftermathTerrainGen::SiteKindToString(S.Kind);
		W.Name = S.Name;
		W.Center = FVector(S.Center.X, S.Center.Y, S.PadHeight);
		W.Radius = S.Radius;
	}
	return Out;
}

TArray<FAftermathWorldRoad> UAftermathWorldBuilderLibrary::GetWorldRoads(int32 Seed)
{
	TArray<FAftermathWorldRoad> Out;
	for (const AftermathTerrainGen::FRoad& R : TerrainFor(Seed).GetLayout().Roads)
	{
		FAftermathWorldRoad& W = Out.AddDefaulted_GetRef();
		for (const FVector2D& P : R.Points)
		{
			W.Points.Add(OnGround(Seed, P));
		}
	}
	return Out;
}

TArray<FAftermathWorldSpawn> UAftermathWorldBuilderLibrary::GetWorldSpawns(int32 Seed)
{
	TArray<FAftermathWorldSpawn> Out;
	for (const AftermathTerrainGen::FSpawnPoint& S : TerrainFor(Seed).GetLayout().Spawns)
	{
		FAftermathWorldSpawn& W = Out.AddDefaulted_GetRef();
		W.Location = OnGround(Seed, S.Location, 100.f);
		W.Yaw = S.Yaw;
	}
	return Out;
}

FString UAftermathWorldBuilderLibrary::DiagnoseNavigationAt(FVector Center, float HalfSize)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return TEXT("no editor world");
	}
	const FBox Box = FBox::BuildAABB(Center, FVector(HalfSize, HalfSize, 50000.f));

	// Load the cells in the box (landscape proxies, buildings...) like the navigation builder does.
	if (UWorldPartition* WP = World->GetWorldPartition())
	{
		UWorldPartitionEditorLoaderAdapter* Loader = WP->CreateEditorLoaderAdapter<FLoaderAdapterShape>(World, Box, TEXT("NavDiag"));
		Loader->GetLoaderAdapter()->SetUserCreated(false);
		Loader->GetLoaderAdapter()->Load();
		World->BlockTillLevelStreamingCompleted();
	}

	FNavigationSystem::AddNavigationSystemToWorld(*World, FNavigationSystemRunMode::EditorWorldPartitionBuildMode);
	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!NavSys)
	{
		return TEXT("no navigation system");
	}

	// Count landscape collision components actually loaded in the box, and whether they have a heightfield.
	int32 LandscapeCollision = 0, LandscapeWithBody = 0;
	for (TObjectIterator<ULandscapeHeightfieldCollisionComponent> It; It; ++It)
	{
		if (It->GetWorld() == World && It->IsRegistered()
			&& Box.Intersect(FBox(It->Bounds.GetBox().Min * FVector(1, 1, 0) - FVector(0, 0, 1e6), It->Bounds.GetBox().Max * FVector(1, 1, 0) + FVector(0, 0, 1e6))))
		{
			++LandscapeCollision;
			LandscapeWithBody += It->IsPhysicsStateCreated() ? 1 : 0;
			if (LandscapeCollision <= 2)
			{
				const ULandscapeComponent* Render = It->GetRenderComponent();
				UE_LOG(LogAftermathWorldBuilder, Display, TEXT("[Aftermath] NavDiag hf=%s local_box=%s world_bounds=%s render_local_box=%s nav_bounds=%s"),
					*It->GetName(), *It->CachedLocalBox.ToString(), *It->Bounds.GetBox().ToString(),
					Render ? *Render->CachedLocalBox.ToString() : TEXT("none"), *It->GetNavigationBounds().ToString());
			}
		}
	}

	// A freshly loaded editor world keeps an async-load build lock until its asset compilation settles
	// (flag 0x20); without releasing it Build() does nothing and we'd only see previously baked tiles.
	const uint8 LocksBefore = NavSys->IsNavigationBuildingLocked() ? 1 : 0;
	NavSys->RemoveNavigationBuildLock(ENavigationBuildLock::AsyncLoadLock, UNavigationSystemV1::ELockRemovalRebuildAction::NoRebuild);
	UE_LOG(LogAftermathWorldBuilder, Display, TEXT("[Aftermath] NavDiag build_locked_before=%d locked_after=%d"),
		LocksBefore, NavSys->IsNavigationBuildingLocked() ? 1 : 0);
	NavSys->SetBuildBounds(Box);
	FNavigationSystem::Build(*World);

	struct FStat { int32 Elements = 0, WithGeom = 0, Pending = 0, Slices = 0; int64 Bytes = 0, Verts = 0, Faces = 0, FacesUp = 0, FacesDown = 0; FBox GeomBounds = FBox(ForceInit); FBox DataBounds = FBox(ForceInit); FBox ElemBounds = FBox(ForceInit); };
	TMap<FString, FStat> ByClass;
	if (const FNavigationOctree* Octree = NavSys->GetNavOctree())
	{
		Octree->FindElementsWithBoundsTest(Box, [&ByClass](const FNavigationOctreeElement& E)
		{
			const UObject* Src = E.Data->SourceElement->GetWeakUObject().Get();
			FStat& S = ByClass.FindOrAdd(Src ? Src->GetClass()->GetName() : TEXT("?"));
			++S.Elements;
			S.WithGeom += E.Data->HasGeometry() ? 1 : 0;
			S.Pending += E.Data->IsPendingLazyGeometryGathering() ? 1 : 0;
			S.Slices += E.Data->SupportsGatheringGeometrySlices() ? 1 : 0;
			S.Bytes += E.Data->CollisionData.Num();
			// Recast clips rasterization to Data->Bounds: if these don't cover the real geometry, nothing is voxelized.
			if (E.Data->Bounds.IsValid)
			{
				S.DataBounds += E.Data->Bounds;
			}
			S.ElemBounds += E.Bounds.GetBox();
			if (E.Data->CollisionData.Num() > 0)
			{
				// The cache stores recast coords (Y up); report them back in Unreal coords.
				const FRecastGeometryCache Cache(E.Data->CollisionData.GetData());
				S.Verts += Cache.Header.NumVerts;
				S.Faces += Cache.Header.NumFaces;
				for (int32 v = 0; v < Cache.Header.NumVerts; ++v)
				{
					S.GeomBounds += Recast2UnrealPoint(&Cache.Verts[v * 3]);
				}
				// Recast marks a triangle walkable when its normal (recast space, Y up) points up enough.
				// Count faces whose normal points up vs down: a flipped winding makes flat ground unwalkable.
				for (int32 f = 0; f < Cache.Header.NumFaces; ++f)
				{
					const FVector::FReal* A = &Cache.Verts[Cache.Indices[f * 3 + 0] * 3];
					const FVector::FReal* B = &Cache.Verts[Cache.Indices[f * 3 + 1] * 3];
					const FVector::FReal* C = &Cache.Verts[Cache.Indices[f * 3 + 2] * 3];
					const FVector E0(B[0] - A[0], B[1] - A[1], B[2] - A[2]);
					const FVector E1(C[0] - A[0], C[1] - A[1], C[2] - A[2]);
					const FVector N = FVector::CrossProduct(E0, E1);   // recast rcCalcTriNormal order
					(N.Y > 0.0 ? S.FacesUp : S.FacesDown)++;
				}
			}
		});
	}
	for (const TPair<FString, FStat>& P : ByClass)
	{
		UE_LOG(LogAftermathWorldBuilder, Display, TEXT("[Aftermath] NavDiag octree class=%s elements=%d with_geometry=%d pending_lazy=%d slices=%d collision_bytes=%lld verts=%lld faces=%lld faces_up=%lld faces_down=%lld geom_min=%s geom_max=%s"),
			*P.Key, P.Value.Elements, P.Value.WithGeom, P.Value.Pending, P.Value.Slices, P.Value.Bytes, P.Value.Verts, P.Value.Faces,
			P.Value.FacesUp, P.Value.FacesDown,
			P.Value.GeomBounds.IsValid ? *P.Value.GeomBounds.Min.ToCompactString() : TEXT("-"),
			P.Value.GeomBounds.IsValid ? *P.Value.GeomBounds.Max.ToCompactString() : TEXT("-"));
		UE_LOG(LogAftermathWorldBuilder, Display, TEXT("[Aftermath] NavDiag bounds class=%s data_bounds=%s..%s element_bounds=%s..%s"),
			*P.Key,
			P.Value.DataBounds.IsValid ? *P.Value.DataBounds.Min.ToCompactString() : TEXT("invalid"),
			P.Value.DataBounds.IsValid ? *P.Value.DataBounds.Max.ToCompactString() : TEXT("invalid"),
			P.Value.ElemBounds.IsValid ? *P.Value.ElemBounds.Min.ToCompactString() : TEXT("invalid"),
			P.Value.ElemBounds.IsValid ? *P.Value.ElemBounds.Max.ToCompactString() : TEXT("invalid"));
	}
	if (const ARecastNavMesh* R = Cast<ARecastNavMesh>(NavSys->GetDefaultNavDataInstance(FNavigationSystem::DontCreate)))
	{
		UE_LOG(LogAftermathWorldBuilder, Display, TEXT("[Aftermath] NavDiag navmesh tile_size=%.0f cell_size=%.1f cell_height=%.1f agent_radius=%.0f agent_height=%.0f max_slope=%.0f max_step=%.0f runtime=%d world_partitioned=%d"),
			R->TileSizeUU, R->GetCellSize(ENavigationDataResolution::Default), R->GetCellHeight(ENavigationDataResolution::Default),
			R->AgentRadius, R->AgentHeight, R->AgentMaxSlope, R->GetAgentMaxStepHeight(ENavigationDataResolution::Default),
			static_cast<int32>(R->GetRuntimeGenerationMode()), R->bIsWorldPartitioned ? 1 : 0);
	}

	int32 Active = -1;
	int32 PolysHere = 0;
	bool bOnMesh = false;
	if (const ARecastNavMesh* Recast = Cast<ARecastNavMesh>(NavSys->GetDefaultNavDataInstance(FNavigationSystem::DontCreate)))
	{
		Active = Recast->GetNumActiveTiles();
		int32 TX = 0, TY = 0;
		if (Recast->GetNavMeshTileXY(Center, TX, TY))
		{
			TArray<FNavTileRef> Refs;
			Recast->GetNavMeshTilesAt(TX, TY, Refs);
			for (const FNavTileRef& R : Refs)
			{
				TArray<FNavPoly> Polys;
				Recast->GetPolysInTile(R, Polys);
				PolysHere += Polys.Num();
			}
		}
		FNavLocation Loc;
		bOnMesh = NavSys->ProjectPointToNavigation(Center, Loc, FVector(300.f, 300.f, 2000.f));
	}
	const FString Summary = FString::Printf(TEXT("landscape_collision=%d with_physics=%d octree_classes=%d active_tiles=%d polys_at_center=%d center_on_mesh=%d"),
		LandscapeCollision, LandscapeWithBody, ByClass.Num(), Active, PolysHere, bOnMesh ? 1 : 0);
	UE_LOG(LogAftermathWorldBuilder, Display, TEXT("[Aftermath] NavDiag %s"), *Summary);
	return Summary;
}

int64 UAftermathWorldBuilderLibrary::GetContentFolderBytes(const FString& ContentPath)
{
	FString Dir;
	if (!FPackageName::TryConvertLongPackageNameToFilename(ContentPath / TEXT("x"), Dir))
	{
		return -1;
	}
	Dir = FPaths::GetPath(Dir);
	int64 Total = 0;
	IFileManager::Get().IterateDirectoryStatRecursively(*Dir, [&Total](const TCHAR*, const FFileStatData& Stat)
	{
		if (!Stat.bIsDirectory)
		{
			Total += Stat.FileSize;
		}
		return true;
	});
	return Total;
}
