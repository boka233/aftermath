#include "World/AftermathLootManager.h"

#include "Aftermath.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Interaction/AftermathPickup.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Player/AftermathCharacter.h"
#include "World/AftermathLootSpawnPoint.h"

bool UAftermathLootManager::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld() && World->GetNetMode() != NM_Client;
}

TStatId UAftermathLootManager::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UAftermathLootManager, STATGROUP_Tickables);
}

bool UAftermathLootManager::LoadConfig()
{
	const FString Path = FPaths::ProjectContentDir() / TEXT("Data/loot.json");
	FString Json;
	FString Error;
	if (!FFileHelper::LoadFileToString(Json, *Path) || !AftermathLoot::ParseConfig(Json, Config, Error))
	{
		UE_LOG(LogAftermath, Error, TEXT("[Aftermath] LootConfig failed path=%s error=%s"), *Path, Error.IsEmpty() ? TEXT("cannot read") : *Error);
		return false;
	}
	UE_LOG(LogAftermath, Display, TEXT("[Aftermath] LootConfig loaded items=%d interval=%.0f batch=%d"),
		Config.Items.Num(), Config.RestockIntervalSeconds, Config.RestockBatch);
	return true;
}

void UAftermathLootManager::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (!bConfigLoaded)
	{
		bConfigLoaded = LoadConfig();
	}
	// First pass shortly after start (spawn points register during BeginPlay).
	Timer = FMath::Max(0.f, Config.RestockIntervalSeconds - 2.f);
}

void UAftermathLootManager::RegisterPoint(AAftermathLootSpawnPoint* Point)
{
	if (Point)
	{
		Points.AddUnique(Point);
	}
}

void UAftermathLootManager::UnregisterPoint(AAftermathLootSpawnPoint* Point)
{
	const int32 Index = Points.IndexOfByKey(Point);
	if (Index != INDEX_NONE)
	{
		// Keep indices stable for tracked items: clear the slot instead of removing it.
		Points[Index].Reset();
	}
}

void UAftermathLootManager::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!bConfigLoaded || Points.Num() == 0)
	{
		return;
	}
	Timer += DeltaTime;
	if (Timer >= Config.RestockIntervalSeconds)
	{
		Timer = 0.f;
		RunPass();
	}
}

int32 UAftermathLootManager::RunPass()
{
	UWorld* World = GetWorld();
	if (!World || !bConfigLoaded)
	{
		return 0;
	}
	const double Now = World->GetTimeSeconds();
	++PassCount;

	TArray<FVector> Players;
	for (TActorIterator<AAftermathCharacter> It(World); It; ++It)
	{
		if (!It->IsDead())
		{
			Players.Add(It->GetActorLocation());
		}
	}
	auto NearPlayer = [&Players, this](const FVector& L)
	{
		for (const FVector& P : Players)
		{
			if (FVector::DistSquared(P, L) < FMath::Square(Config.MinPlayerDistance))
			{
				return true;
			}
		}
		return false;
	};

	// 1. Forget taken items; clean up stale ones.
	int32 Cleaned = 0;
	for (int32 i = Tracked.Num() - 1; i >= 0; --i)
	{
		FTrackedItem& T = Tracked[i];
		AAftermathPickup* P = T.Pickup.Get();
		if (!P || P->IsTaken())
		{
			Tracked.RemoveAtSwap(i);
			continue;
		}
		const AftermathLoot::FItemRule* Rule = Config.Find(T.ItemId);
		if (Rule && AftermathLoot::ShouldCleanup(*Rule, T.SpawnedAt, Now, NearPlayer(P->GetActorLocation())))
		{
			P->Destroy();
			Tracked.RemoveAtSwap(i);
			++Cleaned;
		}
	}

	// 2. Count everything on the ground by item (economy items and player drops alike).
	TMap<FName, int32> Counts;
	for (TActorIterator<AAftermathPickup> It(World); It; ++It)
	{
		if (!It->IsTaken())
		{
			++Counts.FindOrAdd(It->GetItemId());
		}
	}
	TArray<int32> OnGround;
	for (const AftermathLoot::FItemRule& R : Config.Items)
	{
		OnGround.Add(Counts.FindRef(R.Id));
	}

	// 3. Points: occupied if one of our items still lies on it.
	TArray<AftermathLoot::FSpawnPoint> PlanPoints;
	PlanPoints.SetNum(Points.Num());
	for (int32 i = 0; i < Points.Num(); ++i)
	{
		const AAftermathLootSpawnPoint* SP = Points[i].Get();
		PlanPoints[i].Location = SP ? SP->GetActorLocation() : FVector(1e9);
		PlanPoints[i].Tag = SP ? SP->GetLootTag() : NAME_None;
		PlanPoints[i].bOccupied = SP == nullptr;
	}
	for (const FTrackedItem& T : Tracked)
	{
		if (PlanPoints.IsValidIndex(T.PointIndex))
		{
			PlanPoints[T.PointIndex].bOccupied = true;
		}
	}

	// 4. Restock.
	const TArray<AftermathLoot::FPlacement> Plan = AftermathLoot::PlanRestock(Config, OnGround, PlanPoints, Players, PassCount);
	int32 Spawned = 0;
	for (const AftermathLoot::FPlacement& Pl : Plan)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AAftermathPickup* Pickup = World->SpawnActor<AAftermathPickup>(PlanPoints[Pl.PointIndex].Location, FRotator(0.f, FMath::FRandRange(0.f, 360.f), 0.f), Params);
		if (!Pickup)
		{
			continue;
		}
		Pickup->SetItemById(Pl.ItemId, Pl.Count);
		FTrackedItem& T = Tracked.AddDefaulted_GetRef();
		T.Pickup = Pickup;
		T.PointIndex = Pl.PointIndex;
		T.ItemId = Pl.ItemId;
		T.SpawnedAt = Now;
		++Spawned;
	}
	LastSpawnedCount = Spawned;
	LastCleanedCount = Cleaned;
	int32 GroundTotal = 0;
	for (const TPair<FName, int32>& C : Counts)
	{
		GroundTotal += C.Value;
	}
	UE_LOG(LogAftermath, Display, TEXT("[Aftermath] LootPass n=%d points=%d on_ground=%d tracked=%d spawned=%d cleaned=%d players=%d"),
		PassCount, Points.Num(), GroundTotal, Tracked.Num(), Spawned, Cleaned, Players.Num());
	return Spawned;
}
