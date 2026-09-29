// Stage 5 pure loot economy: config parsing, restock planning, cleanup.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#include "Items/AftermathItemDatabase.h"
#include "World/AftermathLootEconomy.h"

namespace AftermathLootTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	using namespace AftermathLoot;

	FConfig SmallConfig()
	{
		FConfig C;
		C.RestockBatch = 100;
		C.MinPlayerDistance = 5000.f;
		FItemRule Beans; Beans.Id = TEXT("CannedBeans"); Beans.Nominal = 10; Beans.Max = 12; Beans.Tags = { TEXT("House") };
		FItemRule Gun;   Gun.Id = TEXT("Rifle");         Gun.Nominal = 2;    Gun.Max = 3;    Gun.Tags = { TEXT("Military") };
		FItemRule Band;  Band.Id = TEXT("Bandage");      Band.Nominal = 4;   Band.Max = 4;   Band.Tags = { TEXT("Medical"), TEXT("House") };
		C.Items = { Beans, Gun, Band };
		return C;
	}

	TArray<FSpawnPoint> Points(int32 Houses, int32 Military, int32 Medical)
	{
		TArray<FSpawnPoint> P;
		auto Add = [&P](FName Tag, int32 N, float X0)
		{
			for (int32 i = 0; i < N; ++i)
			{
				FSpawnPoint S;
				S.Tag = Tag;
				S.Location = FVector(X0 + i * 1000.f, 0.f, 0.f);
				P.Add(S);
			}
		};
		Add(TEXT("House"), Houses, 0.f);
		Add(TEXT("Military"), Military, 200000.f);
		Add(TEXT("Medical"), Medical, 400000.f);
		return P;
	}

	int32 CountOf(const TArray<FPlacement>& L, FName Id)
	{
		int32 N = 0;
		for (const FPlacement& P : L) { N += P.ItemId == Id; }
		return N;
	}
}

using namespace AftermathLootTest;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAftermathLootConfigTest, "Aftermath.Loot.Config", AftermathLootTest::Flags)
bool FAftermathLootConfigTest::RunTest(const FString& Parameters)
{
	// The shipped file parses and every item exists in the item database.
	FString Json;
	UTEST_TRUE("read loot.json", FFileHelper::LoadFileToString(Json, *(FPaths::ProjectContentDir() / TEXT("Data/loot.json"))));
	FConfig C;
	FString Err;
	UTEST_TRUE(FString::Printf(TEXT("parse (%s)"), *Err), ParseConfig(Json, C, Err));
	TestTrue("has items", C.Items.Num() >= 20);
	for (const FItemRule& R : C.Items)
	{
		TestNotNull(FString::Printf(TEXT("%s is a real item"), *R.Id.ToString()), UAftermathItemDatabase::FindDef(R.Id));
	}
	TestTrue("rifles rarer than beans", C.Find(TEXT("Rifle"))->Nominal < C.Find(TEXT("CannedBeans"))->Nominal);

	// Validation.
	FConfig Bad;
	TestFalse("Max < Nominal rejected", ParseConfig(TEXT("{\"Items\":[{\"Id\":\"Apple\",\"Nominal\":5,\"Max\":2,\"Tags\":[\"Farm\"]}]}"), Bad, Err));
	TestFalse("no tags rejected", ParseConfig(TEXT("{\"Items\":[{\"Id\":\"Apple\",\"Nominal\":5}]}"), Bad, Err));
	TestFalse("duplicate rejected", ParseConfig(TEXT("{\"Items\":[{\"Id\":\"Apple\",\"Nominal\":1,\"Tags\":[\"Farm\"]},{\"Id\":\"Apple\",\"Nominal\":1,\"Tags\":[\"Farm\"]}]}"), Bad, Err));
	TestFalse("garbage rejected", ParseConfig(TEXT("not json"), Bad, Err));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAftermathLootRestockTest, "Aftermath.Loot.Restock", AftermathLootTest::Flags)
bool FAftermathLootRestockTest::RunTest(const FString& Parameters)
{
	const FConfig C = SmallConfig();
	const TArray<FSpawnPoint> P = Points(30, 5, 5);

	// Empty map: fills everything up to Nominal, only on matching tags.
	const TArray<FPlacement> First = PlanRestock(C, { 0, 0, 0 }, P, {}, 1);
	TestEqual("beans to nominal", CountOf(First, TEXT("CannedBeans")), 10);
	TestEqual("rifles to nominal", CountOf(First, TEXT("Rifle")), 2);
	TestEqual("bandages to nominal", CountOf(First, TEXT("Bandage")), 4);
	TSet<int32> Seen;
	for (const FPlacement& Pl : First)
	{
		TestFalse("each point used once", Seen.Contains(Pl.PointIndex));
		Seen.Add(Pl.PointIndex);
		const FName Tag = P[Pl.PointIndex].Tag;
		TestTrue(FString::Printf(TEXT("%s on an allowed tag (%s)"), *Pl.ItemId.ToString(), *Tag.ToString()),
			C.Find(Pl.ItemId)->Tags.Contains(Tag));
	}

	// Already at nominal: nothing to do.
	TestEqual("full map, no spawns", PlanRestock(C, { 10, 2, 4 }, P, {}, 1).Num(), 0);
	// Players dropped extra beans (above nominal): still no more beans.
	TestEqual("over nominal, no beans", CountOf(PlanRestock(C, { 15, 0, 0 }, P, {}, 1), TEXT("CannedBeans")), 0);

	// Batch limit, most-starved first: rifles (0/2) and bandages (0/4) go before beans at 9/10.
	FConfig Small = C;
	Small.RestockBatch = 3;
	const TArray<FPlacement> Batch = PlanRestock(Small, { 9, 0, 0 }, P, {}, 1);
	TestEqual("batch size", Batch.Num(), 3);
	TestEqual("no beans while others starve", CountOf(Batch, TEXT("CannedBeans")), 0);

	// Occupied points and points near a player are skipped.
	TArray<FSpawnPoint> Busy = Points(0, 5, 0);
	Busy[0].bOccupied = true;
	// Points are 10 m apart, so a player standing on point 1 is within 50 m of all five.
	const TArray<FVector> Player = { Busy[1].Location };
	TestEqual("player nearby blocks spawns", PlanRestock(C, { 10, 0, 4 }, Busy, Player, 1).Num(), 0);
	const TArray<FPlacement> Far = PlanRestock(C, { 10, 0, 4 }, Busy, { FVector(-1e6, 0, 0) }, 1);
	TestEqual("rifles fill the free military points", CountOf(Far, TEXT("Rifle")), 2);
	for (const FPlacement& Pl : Far)
	{
		TestNotEqual("occupied point skipped", Pl.PointIndex, 0);
	}

	// Not enough points: stops cleanly.
	TestEqual("only 1 military point free", CountOf(PlanRestock(C, { 10, 0, 4 }, Points(0, 1, 0), {}, 1), TEXT("Rifle")), 1);

	// Deterministic with a seed.
	TestTrue("same seed same plan", PlanRestock(C, { 0, 0, 0 }, P, {}, 7)[0].PointIndex == PlanRestock(C, { 0, 0, 0 }, P, {}, 7)[0].PointIndex);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAftermathLootCleanupTest, "Aftermath.Loot.Cleanup", AftermathLootTest::Flags)
bool FAftermathLootCleanupTest::RunTest(const FString& Parameters)
{
	FItemRule R;
	R.Lifetime = 600.f;
	TestFalse("young item stays", ShouldCleanup(R, 100.0, 500.0, false));
	TestTrue("old item goes", ShouldCleanup(R, 100.0, 800.0, false));
	TestFalse("never in front of a player", ShouldCleanup(R, 100.0, 5000.0, true));
	R.Lifetime = 0.f;
	TestFalse("lifetime 0 = forever", ShouldCleanup(R, 0.0, 1e9, false));
	return true;
}

#endif
