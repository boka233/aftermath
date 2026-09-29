#include "World/AftermathLootEconomy.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace AftermathLoot
{
	const FItemRule* FConfig::Find(FName Id) const
	{
		return Items.FindByPredicate([Id](const FItemRule& R) { return R.Id == Id; });
	}

	bool ParseConfig(const FString& Json, FConfig& Out, FString& OutError)
	{
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			OutError = TEXT("loot.json: invalid JSON");
			return false;
		}
		FConfig C;
		double Num = 0.0;
		if (Root->TryGetNumberField(TEXT("RestockIntervalSeconds"), Num)) { C.RestockIntervalSeconds = static_cast<float>(Num); }
		if (Root->TryGetNumberField(TEXT("RestockBatch"), Num)) { C.RestockBatch = static_cast<int32>(Num); }
		if (Root->TryGetNumberField(TEXT("MinPlayerDistance"), Num)) { C.MinPlayerDistance = static_cast<float>(Num); }

		const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
		if (!Root->TryGetArrayField(TEXT("Items"), Items))
		{
			OutError = TEXT("loot.json: missing Items array");
			return false;
		}
		for (const TSharedPtr<FJsonValue>& V : *Items)
		{
			const TSharedPtr<FJsonObject> O = V.IsValid() ? V->AsObject() : nullptr;
			if (!O.IsValid())
			{
				OutError = TEXT("loot.json: Items entry is not an object");
				return false;
			}
			FItemRule R;
			FString Id;
			if (!O->TryGetStringField(TEXT("Id"), Id) || Id.IsEmpty())
			{
				OutError = TEXT("loot.json: item without Id");
				return false;
			}
			R.Id = FName(*Id);
			if (O->TryGetNumberField(TEXT("Nominal"), Num)) { R.Nominal = static_cast<int32>(Num); }
			R.Max = R.Nominal;
			if (O->TryGetNumberField(TEXT("Max"), Num)) { R.Max = static_cast<int32>(Num); }
			if (O->TryGetNumberField(TEXT("Count"), Num)) { R.Count = FMath::Max(1, static_cast<int32>(Num)); }
			if (O->TryGetNumberField(TEXT("Lifetime"), Num)) { R.Lifetime = static_cast<float>(Num); }
			const TArray<TSharedPtr<FJsonValue>>* Tags = nullptr;
			if (O->TryGetArrayField(TEXT("Tags"), Tags))
			{
				for (const TSharedPtr<FJsonValue>& T : *Tags)
				{
					R.Tags.Add(FName(*T->AsString()));
				}
			}
			if (R.Nominal < 0 || R.Max < R.Nominal)
			{
				OutError = FString::Printf(TEXT("loot.json: %s has Max (%d) < Nominal (%d)"), *Id, R.Max, R.Nominal);
				return false;
			}
			if (R.Tags.Num() == 0)
			{
				OutError = FString::Printf(TEXT("loot.json: %s has no Tags (it could never spawn)"), *Id);
				return false;
			}
			if (C.Find(R.Id))
			{
				OutError = FString::Printf(TEXT("loot.json: %s listed twice"), *Id);
				return false;
			}
			C.Items.Add(MoveTemp(R));
		}
		Out = MoveTemp(C);
		return true;
	}

	TArray<FPlacement> PlanRestock(const FConfig& Config, const TArray<int32>& OnGround,
		const TArray<FSpawnPoint>& Points, const TArray<FVector>& Players, int32 Seed)
	{
		TArray<FPlacement> Out;
		if (Config.Items.Num() == 0 || Points.Num() == 0 || Config.RestockBatch <= 0)
		{
			return Out;
		}

		// Free points per tag, excluding occupied ones and those a player could see appear.
		const float MinD2 = FMath::Square(Config.MinPlayerDistance);
		TMap<FName, TArray<int32>> FreeByTag;
		for (int32 i = 0; i < Points.Num(); ++i)
		{
			const FSpawnPoint& P = Points[i];
			if (P.bOccupied)
			{
				continue;
			}
			bool bSeen = false;
			for (const FVector& Pl : Players)
			{
				if (FVector::DistSquared(Pl, P.Location) < MinD2)
				{
					bSeen = true;
					break;
				}
			}
			if (!bSeen)
			{
				FreeByTag.FindOrAdd(P.Tag).Add(i);
			}
		}

		// Shuffle each tag's list deterministically so restocks spread across the map.
		FRandomStream Rng(Seed);
		for (TPair<FName, TArray<int32>>& Pair : FreeByTag)
		{
			TArray<int32>& L = Pair.Value;
			for (int32 i = L.Num() - 1; i > 0; --i)
			{
				L.Swap(i, Rng.RandRange(0, i));
			}
		}

		// Working counts; fill the most-starved item first, one at a time.
		TArray<int32> Have;
		Have.SetNumZeroed(Config.Items.Num());
		for (int32 i = 0; i < Config.Items.Num(); ++i)
		{
			Have[i] = OnGround.IsValidIndex(i) ? OnGround[i] : 0;
		}
		TSet<int32> Used;
		while (Out.Num() < Config.RestockBatch)
		{
			int32 Best = INDEX_NONE;
			float BestFill = 1.f;
			for (int32 i = 0; i < Config.Items.Num(); ++i)
			{
				const FItemRule& R = Config.Items[i];
				if (R.Nominal <= 0 || Have[i] >= R.Nominal || Have[i] >= R.Max)
				{
					continue;
				}
				const float Fill = static_cast<float>(Have[i]) / R.Nominal;
				if (Fill < BestFill)
				{
					BestFill = Fill;
					Best = i;
				}
			}
			if (Best == INDEX_NONE)
			{
				break; // everything is at Nominal
			}

			// A free point with one of the item's tags (tags tried in random order).
			const FItemRule& R = Config.Items[Best];
			int32 Point = INDEX_NONE;
			const int32 Start = Rng.RandRange(0, R.Tags.Num() - 1);
			for (int32 k = 0; k < R.Tags.Num() && Point == INDEX_NONE; ++k)
			{
				if (TArray<int32>* L = FreeByTag.Find(R.Tags[(Start + k) % R.Tags.Num()]))
				{
					while (L->Num() > 0 && Point == INDEX_NONE)
					{
						const int32 Candidate = L->Pop(EAllowShrinking::No);
						if (!Used.Contains(Candidate))
						{
							Point = Candidate;
						}
					}
				}
			}
			if (Point == INDEX_NONE)
			{
				// No room for this item right now: stop asking for it this pass.
				Have[Best] = R.Nominal;
				continue;
			}
			Used.Add(Point);
			FPlacement& Pl = Out.AddDefaulted_GetRef();
			Pl.ItemId = R.Id;
			Pl.Count = R.Count;
			Pl.PointIndex = Point;
			++Have[Best];
		}
		return Out;
	}

	bool ShouldCleanup(const FItemRule& Rule, double SpawnedAt, double Now, bool bNearPlayer)
	{
		// Never vanish in front of someone.
		return !bNearPlayer && Rule.Lifetime > 0.f && Now - SpawnedAt >= Rule.Lifetime;
	}
}
