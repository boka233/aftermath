// Stage 5 pure world generation: coastline, hills, settlements, roads, spawns, heightmap encoding.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"

#include "World/AftermathTerrainGen.h"

namespace AftermathTerrainGenTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	using namespace AftermathTerrainGen;

	const FTerrain& World()
	{
		static const FTerrain T{ FSettings() };
		return T;
	}
}

using namespace AftermathTerrainGenTest;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAftermathTerrainShapeTest, "Aftermath.World.TerrainShape", AftermathTerrainGenTest::Flags)
bool FAftermathTerrainShapeTest::RunTest(const FString& Parameters)
{
	const FTerrain& T = World();
	const float Half = T.GetSettings().SizeCm * 0.5f;

	// Sea to the south-east, land to the north-west.
	TestTrue("south-east corner is sea", T.HeightAt(FVector2D(-Half * 0.98f, Half * 0.98f)) < 0.f);
	TestTrue("south edge middle is sea", T.HeightAt(FVector2D(-Half * 0.98f, 0.f)) < 0.f);
	TestTrue("east edge middle is sea", T.HeightAt(FVector2D(0.f, Half * 0.98f)) < 0.f);
	TestTrue("north-west corner is high land", T.HeightAt(FVector2D(Half * 0.9f, -Half * 0.9f)) > 5000.f);

	// Sample the whole map on a 100 m grid.
	int32 Land = 0, Sea = 0;
	float MaxH = -1e9f, MinH = 1e9f;
	float MaxSlope = 0.f;
	int32 Steep = 0;
	const int32 N = 100;
	for (int32 i = 0; i <= N; ++i)
	{
		for (int32 j = 0; j <= N; ++j)
		{
			const FVector2D P(-Half + i * (2.f * Half / N), -Half + j * (2.f * Half / N));
			const float H = T.HeightAt(P);
			MaxH = FMath::Max(MaxH, H);
			MinH = FMath::Min(MinH, H);
			(H > 0.f ? Land : Sea)++;
			if (H > 0.f)
			{
				const float S = T.SlopeAt(P);
				MaxSlope = FMath::Max(MaxSlope, S);
				Steep += S > 1.f ? 1 : 0; // steeper than 45 degrees
			}
		}
	}
	const float LandFrac = static_cast<float>(Land) / (Land + Sea);
	AddInfo(FString::Printf(TEXT("land=%.0f%% height %.0f..%.0f m max_slope=%.2f steep_samples=%d"),
		LandFrac * 100.f, MinH / 100.f, MaxH / 100.f, MaxSlope, Steep));
	TestTrue("mostly land (60-90%)", LandFrac > 0.6f && LandFrac < 0.9f);
	TestTrue("hills reach 150 m+", MaxH > 15000.f);
	TestTrue("stays under the max", MaxH <= T.GetSettings().MaxHeightCm * 1.05f);
	TestTrue("sea floor not deeper than set", MinH >= -T.GetSettings().SeaDepthCm - 200.f);
	TestTrue("rolling hills, not cliffs", Steep < (Land / 50));

	// Deterministic from the seed; a different seed gives a different map.
	const FTerrain Again{ FSettings() };
	TestEqual("same seed, same height", Again.HeightAt(FVector2D(12345.f, -67890.f)), T.HeightAt(FVector2D(12345.f, -67890.f)));
	FSettings Other;
	Other.Seed = 42;
	const FTerrain Diff{ Other };
	TestNotEqual("different seed, different map", Diff.HeightAt(FVector2D(12345.f, -67890.f)), T.HeightAt(FVector2D(12345.f, -67890.f)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAftermathTerrainLayoutTest, "Aftermath.World.Layout", AftermathTerrainGenTest::Flags)
bool FAftermathTerrainLayoutTest::RunTest(const FString& Parameters)
{
	const FTerrain& T = World();
	const FLayout& L = T.GetLayout();

	int32 Cities = 0, Towns = 0, Villages = 0, Military = 0;
	for (const FSite& S : L.Sites)
	{
		Cities += S.Kind == ESiteKind::City;
		Towns += S.Kind == ESiteKind::Town;
		Villages += S.Kind == ESiteKind::Village;
		Military += (S.Kind == ESiteKind::Airfield || S.Kind == ESiteKind::Base);

		TestTrue(FString::Printf(TEXT("%s is on land"), *S.Name), T.HeightAt(S.Center) > 100.f);
		// The pad is flat: heights across the core radius differ by < 1.5 m.
		const float H0 = T.HeightAt(S.Center);
		float MaxDiff = 0.f;
		for (int32 a = 0; a < 8; ++a)
		{
			const float Ang = a * PI / 4.f;
			const FVector2D P = S.Center + FVector2D(FMath::Cos(Ang), FMath::Sin(Ang)) * S.Radius * 0.8f;
			MaxDiff = FMath::Max(MaxDiff, FMath::Abs(T.HeightAt(P) - H0));
		}
		TestTrue(FString::Printf(TEXT("%s pad is flat (%.1f m)"), *S.Name, MaxDiff / 100.f), MaxDiff < 150.f);
	}
	TestEqual("one city", Cities, 1);
	TestEqual("three towns", Towns, 3);
	TestEqual("six villages", Villages, 6);
	TestEqual("three military sites", Military, 3);

	// The city is a harbour: sea within 600 m of its centre.
	for (const FSite& S : L.Sites)
	{
		if (S.Kind != ESiteKind::City)
		{
			continue;
		}
		bool bSeaNear = false;
		for (int32 a = 0; a < 16 && !bSeaNear; ++a)
		{
			const float Ang = a * PI / 8.f;
			bSeaNear = T.HeightAt(S.Center + FVector2D(FMath::Cos(Ang), FMath::Sin(Ang)) * 60000.f) < 0.f;
		}
		TestTrue("city is on the coast", bSeaNear);
	}

	// Sites don't overlap each other.
	for (int32 i = 0; i < L.Sites.Num(); ++i)
	{
		for (int32 j = i + 1; j < L.Sites.Num(); ++j)
		{
			const float D = static_cast<float>(FVector2D::Distance(L.Sites[i].Center, L.Sites[j].Center));
			if (D < (L.Sites[i].Radius + L.Sites[j].Radius) * 0.8f)
			{
				AddError(FString::Printf(TEXT("%s and %s overlap (%.0f m apart)"), *L.Sites[i].Name, *L.Sites[j].Name, D / 100.f));
			}
		}
	}

	// Roads: every site is reachable from the first one.
	TArray<bool> Seen;
	Seen.Init(false, L.Sites.Num());
	Seen[0] = true;
	for (bool bChanged = true; bChanged;)
	{
		bChanged = false;
		for (const FRoad& R : L.Roads)
		{
			if (Seen[R.From] != Seen[R.To])
			{
				Seen[R.From] = Seen[R.To] = true;
				bChanged = true;
			}
		}
	}
	TestFalse("every site connected by road", Seen.Contains(false));

	// Spawns: on the beach (land, low), with sea close by in front of their back.
	TestEqual("ten coastal spawns", L.Spawns.Num(), 10);
	for (const FSpawnPoint& S : L.Spawns)
	{
		const float H = T.HeightAt(S.Location);
		const FVector2D Back = S.Location - FVector2D(FMath::Cos(FMath::DegreesToRadians(S.Yaw)), FMath::Sin(FMath::DegreesToRadians(S.Yaw))) * 15000.f;
		TestTrue(FString::Printf(TEXT("spawn at %s is on land (%.1f m)"), *S.Location.ToString(), H / 100.f), H > 50.f && H < 3000.f);
		TestTrue("sea behind the spawn", T.HeightAt(Back) < 0.f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAftermathTerrainHeightmapTest, "Aftermath.World.Heightmap", AftermathTerrainGenTest::Flags)
bool FAftermathTerrainHeightmapTest::RunTest(const FString& Parameters)
{
	const float Z = 100.f;
	TestEqual("sea level encodes to mid", EncodeHeight(0.f, Z), static_cast<uint16>(32768));
	TestTrue("round trip", FMath::IsNearlyEqual(DecodeHeight(EncodeHeight(12345.f, Z), Z), 12345.f, 1.f));
	TestEqual("clamped high", EncodeHeight(1e9f, Z), static_cast<uint16>(65535));
	TestEqual("clamped low", EncodeHeight(-1e9f, Z), static_cast<uint16>(0));

	// A small preview heightmap: size, and the corners agree with HeightAt.
	const FTerrain& T = World();
	const float Half = T.GetSettings().SizeCm * 0.5f;
	TArray<uint16> H;
	const int32 V = 65;
	T.FillHeightmap(V, Z, H);
	TestEqual("size", H.Num(), V * V);
	TestTrue("south-west corner sample", FMath::IsNearlyEqual(DecodeHeight(H[0], Z), T.HeightAt(FVector2D(-Half, -Half)), 1.f));
	TestTrue("north-east corner sample", FMath::IsNearlyEqual(DecodeHeight(H[V * V - 1], Z), T.HeightAt(FVector2D(Half, Half)), 1.f));
	// Row = Y: row 0 col V-1 is (X=+Half, Y=-Half) = the north-west corner (high land).
	TestTrue("row/col mapping", DecodeHeight(H[V - 1], Z) > 5000.f);

	const FString Path = FPaths::ProjectSavedDir() / TEXT("Tests/terrain_preview.r16");
	TestTrue("write r16", T.WriteR16(Path, V, Z));
	TestEqual("r16 size", IFileManager::Get().FileSize(*Path), static_cast<int64>(V * V * 2));
	IFileManager::Get().Delete(*Path);
	return true;
}

#endif
