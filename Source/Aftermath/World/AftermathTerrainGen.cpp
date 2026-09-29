#include "World/AftermathTerrainGen.h"

#include "Async/ParallelFor.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace AftermathTerrainGen
{
	namespace
	{
		uint32 Hash2(int32 X, int32 Y, int32 Seed)
		{
			uint32 H = static_cast<uint32>(X) * 0x8da6b343u ^ static_cast<uint32>(Y) * 0xd8163841u ^ static_cast<uint32>(Seed) * 0xcb1ab31fu;
			H ^= H >> 13;
			H *= 0x5bd1e995u;
			H ^= H >> 15;
			return H;
		}

		/** Lattice value in [-1, 1]. */
		float Lattice(int32 X, int32 Y, int32 Seed)
		{
			return static_cast<float>(Hash2(X, Y, Seed) & 0xffffff) / static_cast<float>(0x7fffff) - 1.f;
		}

		float Smooth(float T) { return T * T * (3.f - 2.f * T); }

		float ValueNoise(const FVector2D& P, int32 Seed)
		{
			const int32 X0 = FMath::FloorToInt(P.X);
			const int32 Y0 = FMath::FloorToInt(P.Y);
			const float FX = Smooth(static_cast<float>(P.X - X0));
			const float FY = Smooth(static_cast<float>(P.Y - Y0));
			const float A = Lattice(X0, Y0, Seed);
			const float B = Lattice(X0 + 1, Y0, Seed);
			const float C = Lattice(X0, Y0 + 1, Seed);
			const float D = Lattice(X0 + 1, Y0 + 1, Seed);
			return FMath::Lerp(FMath::Lerp(A, B, FX), FMath::Lerp(C, D, FX), FY);
		}

		/** Deterministic 0..1 random from an index (layout decisions). */
		float Rand01(int32 Seed, int32 Salt, int32 I)
		{
			return static_cast<float>(Hash2(Salt, I, Seed) & 0xffff) / 65535.f;
		}

		float SmoothStep(float E0, float E1, float X)
		{
			const float T = FMath::Clamp((X - E0) / FMath::Max(E1 - E0, 1e-3f), 0.f, 1.f);
			return T * T * (3.f - 2.f * T);
		}
	}

	float FractalNoise(const FVector2D& P, float Frequency, int32 Octaves, int32 Seed)
	{
		float Sum = 0.f;
		float Amp = 1.f;
		float Norm = 0.f;
		FVector2D Q = P * Frequency;
		for (int32 O = 0; O < Octaves; ++O)
		{
			Sum += ValueNoise(Q, Seed + O * 1013) * Amp;
			Norm += Amp;
			Amp *= 0.5f;
			// Rotate a little each octave to hide the lattice axes.
			Q = FVector2D(Q.X * 1.6f - Q.Y * 1.2f, Q.X * 1.2f + Q.Y * 1.6f);
		}
		return Sum / FMath::Max(Norm, 1e-3f);
	}

	float DistToSegment(const FVector2D& P, const FVector2D& A, const FVector2D& B)
	{
		const FVector2D AB = B - A;
		const double Len2 = AB.SizeSquared();
		const double T = Len2 > 0.0 ? FMath::Clamp(FVector2D::DotProduct(P - A, AB) / Len2, 0.0, 1.0) : 0.0;
		return static_cast<float>(FVector2D::Distance(P, A + AB * T));
	}

	const TCHAR* SiteKindToString(ESiteKind Kind)
	{
		switch (Kind)
		{
		case ESiteKind::City:     return TEXT("city");
		case ESiteKind::Town:     return TEXT("town");
		case ESiteKind::Village:  return TEXT("village");
		case ESiteKind::Airfield: return TEXT("airfield");
		case ESiteKind::Base:     return TEXT("base");
		case ESiteKind::Farm:     return TEXT("farm");
		default:                  return TEXT("?");
		}
	}

	uint16 EncodeHeight(float HeightCm, float ZScale)
	{
		// Clamp in float first: huge inputs would overflow the int conversion.
		const float V = FMath::Clamp(32768.f + HeightCm / FMath::Max(ZScale, 1e-3f) * 128.f, 0.f, 65535.f);
		return static_cast<uint16>(FMath::RoundToInt(V));
	}

	float DecodeHeight(uint16 Value, float ZScale)
	{
		return (static_cast<float>(Value) - 32768.f) / 128.f * ZScale;
	}

	// ---------------------------------------------------------------------------
	// Terrain
	// ---------------------------------------------------------------------------

	FTerrain::FTerrain(const FSettings& InSettings)
		: Settings(InSettings)
		, Half(InSettings.SizeCm * 0.5f)
	{
		BuildLayout();
	}

	float FTerrain::CoastField(const FVector2D& P) const
	{
		// Distance inland from the south (-X) and east (+Y) edges, combined smoothly (the corner rounds off).
		const float FromSouth = static_cast<float>(P.X) + Half;   // 0 at the south edge
		const float FromEast = Half - static_cast<float>(P.Y);    // 0 at the east edge
		const float K = 60000.f;
		// Smooth minimum of the two distances.
		const float H = FMath::Clamp(0.5f + 0.5f * (FromEast - FromSouth) / K, 0.f, 1.f);
		const float Inland = FMath::Lerp(FromEast, FromSouth, H) - K * H * (1.f - H);

		// Wandering shoreline: large bays + smaller wiggles.
		const float Wobble = FractalNoise(P, 1.f / 180000.f, 3, Settings.Seed + 7) * 90000.f
			+ FractalNoise(P, 1.f / 40000.f, 2, Settings.Seed + 11) * 15000.f;

		// A peninsula poking south-east from the south coast around Y = +15% of the map.
		const FVector2D PenA(-Half + 40000.f, Half * 0.15f);
		const FVector2D PenB(-Half + 5000.f, Half * 0.35f);
		const float Pen = FMath::Max(0.f, 45000.f - DistToSegment(P, PenA, PenB)) * 1.6f;

		return Inland - Settings.SeaBandCm + Wobble + Pen;
	}

	float FTerrain::NaturalHeightAt(const FVector2D& P) const
	{
		const float Coast = CoastField(P);
		if (Coast <= 0.f)
		{
			// Sea floor slopes down from the shore to SeaDepth.
			return -Settings.SeaDepthCm * SmoothStep(0.f, 60000.f, -Coast) - 150.f;
		}
		// Normalised inland position 0 (coast) .. 1 (far NW corner) - the land rises toward the plateau.
		const float NW = FMath::Clamp(static_cast<float>((P.X + Half) - P.Y + Half) / (Settings.SizeCm * 2.f), 0.f, 1.f);
		const float Rise = SmoothStep(0.f, Settings.BeachWidthCm * 6.f, Coast);   // beach -> hills over ~700 m

		// Rolling hills: broad swells + medium hills + small bumps.
		const float Broad = FractalNoise(P, 1.f / 250000.f, 3, Settings.Seed + 1) * 0.5f + 0.5f;
		const float Hills = FractalNoise(P, 1.f / 60000.f, 4, Settings.Seed + 2) * 0.5f + 0.5f;
		const float Bumps = FractalNoise(P, 1.f / 9000.f, 2, Settings.Seed + 3);

		// Valleys: a ridged noise channel, carved deeper near the sea (rivers heading downhill).
		const float Ridge = 1.f - FMath::Abs(FractalNoise(P, 1.f / 120000.f, 3, Settings.Seed + 5));
		const float Valley = FMath::Pow(FMath::Clamp(Ridge, 0.f, 1.f), 8.f);

		const float Max = Settings.MaxHeightCm;
		float H = 200.f                                   // beaches sit ~2 m above sea level
			+ Rise * (Max * (0.25f + 0.75f * NW) * (0.35f * Broad + 0.65f * Hills))
			+ Rise * Bumps * 250.f;
		H -= Valley * Rise * Max * 0.25f;
		return FMath::Max(H, 60.f);
	}

	float FTerrain::HeightAt(const FVector2D& P) const
	{
		float H = NaturalHeightAt(P);
		// Roads first: pull the ground toward a smoothed height along the centre line (gentle cut and fill).
		if (Layout.Roads.Num() > 0 && H > 0.f)
		{
			const float RoadD = DistanceToRoad(P);
			if (RoadD < 1500.f)
			{
				const float W = 1.f - SmoothStep(400.f, 1500.f, RoadD);
				// Local average of the natural ground is a cheap stand-in for a proper road profile.
				const float Avg = 0.25f * (NaturalHeightAt(P + FVector2D(2000, 0)) + NaturalHeightAt(P - FVector2D(2000, 0))
					+ NaturalHeightAt(P + FVector2D(0, 2000)) + NaturalHeightAt(P - FVector2D(0, 2000)));
				H = FMath::Lerp(H, FMath::Max(Avg, 80.f), W * 0.7f);
			}
		}
		// Settlement pads last, so they win over roads: fully flat inside the radius, blending out beyond it.
		// Use the strongest pad weight (overlapping blend zones never fight over the core).
		float BestW = 0.f;
		float PadH = 0.f;
		for (const FSite& S : Layout.Sites)
		{
			const float D = static_cast<float>(FVector2D::Distance(P, S.Center));
			const float Outer = S.Radius * 1.6f;
			if (D < Outer)
			{
				const float W = 1.f - SmoothStep(S.Radius, Outer, D);
				if (W > BestW)
				{
					BestW = W;
					PadH = S.PadHeight;
				}
			}
		}
		return BestW > 0.f ? FMath::Lerp(H, PadH, BestW) : H;
	}

	float FTerrain::SlopeAt(const FVector2D& P) const
	{
		const float E = 200.f;
		const float DX = (HeightAt(P + FVector2D(E, 0)) - HeightAt(P - FVector2D(E, 0))) / (2.f * E);
		const float DY = (HeightAt(P + FVector2D(0, E)) - HeightAt(P - FVector2D(0, E))) / (2.f * E);
		return FMath::Sqrt(DX * DX + DY * DY);
	}

	float FTerrain::DistanceToRoad(const FVector2D& P) const
	{
		float Best = TNumericLimits<float>::Max();
		for (const FRoad& R : Layout.Roads)
		{
			for (int32 i = 1; i < R.Points.Num(); ++i)
			{
				Best = FMath::Min(Best, DistToSegment(P, R.Points[i - 1], R.Points[i]));
			}
		}
		return Best;
	}

	FVector2D FTerrain::FindCoastPoint(const FVector2D& From, const FVector2D& Dir) const
	{
		// March from a land point toward the sea until the coast field crosses zero.
		FVector2D P = From;
		const FVector2D Step = Dir.GetSafeNormal() * 2000.f;
		for (int32 i = 0; i < 1000; ++i)
		{
			if (CoastField(P) <= 0.f || FMath::Abs(P.X) > Half || FMath::Abs(P.Y) > Half)
			{
				return P - Step;
			}
			P += Step;
		}
		return P;
	}

	FVector2D FTerrain::FindLandSpot(const FVector2D& Near, float SearchRadius, float MinHeight, float MaxHeight, int32 Salt) const
	{
		// Pick the flattest candidate in the height band among a fixed set of samples.
		FVector2D Best = Near;
		float BestScore = TNumericLimits<float>::Max();
		for (int32 i = 0; i < 48; ++i)
		{
			const float A = Rand01(Settings.Seed, Salt, i) * 2.f * PI;
			const float R = FMath::Sqrt(Rand01(Settings.Seed, Salt + 1, i)) * SearchRadius;
			const FVector2D C = Near + FVector2D(FMath::Cos(A), FMath::Sin(A)) * R;
			if (FMath::Abs(C.X) > Half * 0.94f || FMath::Abs(C.Y) > Half * 0.94f)
			{
				continue;
			}
			const float H = NaturalHeightAt(C);
			if (H < MinHeight || H > MaxHeight || CoastField(C) < 3000.f)
			{
				continue;
			}
			const float E = 3000.f;
			const float Slope = FMath::Abs(NaturalHeightAt(C + FVector2D(E, 0)) - NaturalHeightAt(C - FVector2D(E, 0)))
				+ FMath::Abs(NaturalHeightAt(C + FVector2D(0, E)) - NaturalHeightAt(C - FVector2D(0, E)));
			const float Score = Slope + static_cast<float>(FVector2D::Distance(C, Near)) * 0.002f;
			if (Score < BestScore)
			{
				BestScore = Score;
				Best = C;
			}
		}
		return Best;
	}

	void FTerrain::BuildLayout()
	{
		Layout = FLayout();
		PlaceSites();
		PlaceRoads();
		PlaceSpawns();
	}

	void FTerrain::PlaceSites()
	{
		auto Add = [this](ESiteKind Kind, const FString& Name, const FVector2D& Want, float Radius, float Search, float MinH, float MaxH, int32 Salt)
		{
			FSite S;
			S.Kind = Kind;
			S.Name = Name;
			S.Center = FindLandSpot(Want, Search, MinH, MaxH, Salt);
			S.Radius = Radius;
			S.PadHeight = FMath::Max(NaturalHeightAt(S.Center), 250.f);
			Layout.Sites.Add(S);
		};

		// Harbour city on the south-east coast bend, just inland from the shore.
		const FVector2D CityCoast = FindCoastPoint(FVector2D(-Half * 0.25f, Half * 0.25f), FVector2D(-1.f, 1.f));
		Add(ESiteKind::City, TEXT("Portovo"), CityCoast + FVector2D(9000.f, -9000.f), 26000.f, 15000.f, 200.f, 3000.f, 100);

		// Towns: one on the south coast, one on the east coast, one inland in the middle.
		const FVector2D SouthCoast = FindCoastPoint(FVector2D(-Half * 0.2f, -Half * 0.45f), FVector2D(-1.f, 0.f));
		Add(ESiteKind::Town, TEXT("Beregovo"), SouthCoast + FVector2D(12000.f, 0.f), 17000.f, 20000.f, 200.f, 4000.f, 200);
		const FVector2D EastCoast = FindCoastPoint(FVector2D(Half * 0.35f, Half * 0.2f), FVector2D(0.f, 1.f));
		Add(ESiteKind::Town, TEXT("Vostok"), EastCoast + FVector2D(0.f, -12000.f), 17000.f, 20000.f, 200.f, 4000.f, 300);
		Add(ESiteKind::Town, TEXT("Sredny"), FVector2D(Half * 0.05f, -Half * 0.05f), 17000.f, 60000.f, 1500.f, 20000.f, 400);

		// Villages scattered inland.
		const FVector2D VillageWant[] = {
			FVector2D(-Half * 0.45f, -Half * 0.75f), FVector2D(Half * 0.25f, -Half * 0.55f), FVector2D(Half * 0.6f, -Half * 0.2f),
			FVector2D(-Half * 0.1f, Half * 0.55f),   FVector2D(Half * 0.55f, Half * 0.55f),  FVector2D(-Half * 0.55f, -Half * 0.1f) };
		const TCHAR* VillageNames[] = { TEXT("Lipki"), TEXT("Gorka"), TEXT("Sosnovka"), TEXT("Rybachye"), TEXT("Polyana"), TEXT("Kamenka") };
		for (int32 i = 0; i < UE_ARRAY_COUNT(VillageWant); ++i)
		{
			Add(ESiteKind::Village, VillageNames[i], VillageWant[i], 9000.f, 50000.f, 300.f, 25000.f, 500 + i * 10);
		}

		// Military: airfield on the north-west plateau (needs a long flat strip), two bases.
		Add(ESiteKind::Airfield, TEXT("Airfield"), FVector2D(Half * 0.6f, -Half * 0.6f), 40000.f, 60000.f, 3000.f, 40000.f, 700);
		Add(ESiteKind::Base, TEXT("BaseNorth"), FVector2D(Half * 0.75f, Half * 0.15f), 12000.f, 50000.f, 1500.f, 40000.f, 800);
		Add(ESiteKind::Base, TEXT("BaseWest"), FVector2D(-Half * 0.05f, -Half * 0.8f), 12000.f, 50000.f, 1500.f, 40000.f, 900);

		// Farms along the way.
		for (int32 i = 0; i < 6; ++i)
		{
			const FVector2D Want((Rand01(Settings.Seed, 1000, i) * 1.4f - 0.7f) * Half, (Rand01(Settings.Seed, 1001, i) * 1.4f - 0.7f) * Half);
			Add(ESiteKind::Farm, FString::Printf(TEXT("Farm%d"), i + 1), Want, 5000.f, 40000.f, 400.f, 20000.f, 1100 + i * 10);
		}
	}

	void FTerrain::PlaceRoads()
	{
		// Connect every non-farm site to its nearest neighbour already in the network (a spanning tree),
		// then add a coastal road linking the coastal city and towns. Farms join the nearest site.
		TArray<int32> Main;
		for (int32 i = 0; i < Layout.Sites.Num(); ++i)
		{
			if (Layout.Sites[i].Kind != ESiteKind::Farm)
			{
				Main.Add(i);
			}
		}
		auto AddRoad = [this](int32 A, int32 B)
		{
			FRoad R;
			R.From = A;
			R.To = B;
			const FVector2D PA = Layout.Sites[A].Center;
			const FVector2D PB = Layout.Sites[B].Center;
			// Gentle bends: offset interior points sideways with low-frequency noise.
			const FVector2D Dir = (PB - PA).GetSafeNormal();
			const FVector2D Side(-Dir.Y, Dir.X);
			const int32 N = FMath::Max(2, FMath::CeilToInt(static_cast<float>(FVector2D::Distance(PA, PB)) / 20000.f));
			for (int32 k = 0; k <= N; ++k)
			{
				const float T = static_cast<float>(k) / N;
				FVector2D P = FMath::Lerp(PA, PB, T);
				const float Bend = FMath::Sin(T * PI) * FractalNoise(P, 1.f / 80000.f, 2, Settings.Seed + 31 + A * 7 + B) * 12000.f;
				P += Side * Bend;
				// Keep roads out of the sea.
				if (CoastField(P) < 2000.f)
				{
					P = FMath::Lerp(PA, PB, T);
				}
				R.Points.Add(P);
			}
			Layout.Roads.Add(R);
		};

		TArray<int32> Connected;
		if (Main.Num() > 0)
		{
			Connected.Add(Main[0]);
		}
		TArray<int32> Remaining = Main;
		Remaining.RemoveAt(0);
		while (Remaining.Num() > 0)
		{
			int32 BestR = 0;
			int32 BestC = Connected[0];
			double BestD = TNumericLimits<double>::Max();
			for (int32 r = 0; r < Remaining.Num(); ++r)
			{
				for (int32 c : Connected)
				{
					const double D = FVector2D::DistSquared(Layout.Sites[Remaining[r]].Center, Layout.Sites[c].Center);
					if (D < BestD)
					{
						BestD = D;
						BestR = r;
						BestC = c;
					}
				}
			}
			AddRoad(BestC, Remaining[BestR]);
			Connected.Add(Remaining[BestR]);
			Remaining.RemoveAt(BestR);
		}
		for (int32 i = 0; i < Layout.Sites.Num(); ++i)
		{
			if (Layout.Sites[i].Kind != ESiteKind::Farm)
			{
				continue;
			}
			int32 Nearest = INDEX_NONE;
			double BestD = TNumericLimits<double>::Max();
			for (int32 m : Main)
			{
				const double D = FVector2D::DistSquared(Layout.Sites[i].Center, Layout.Sites[m].Center);
				if (D < BestD) { BestD = D; Nearest = m; }
			}
			if (Nearest != INDEX_NONE)
			{
				AddRoad(Nearest, i);
			}
		}
	}

	void FTerrain::PlaceSpawns()
	{
		// DayZ-style: fresh spawns along the coast, spread out, facing inland.
		const int32 Count = 10;
		for (int32 i = 0; i < Count; ++i)
		{
			// Walk the coast from the south-west corner along the south coast and up the east coast.
			const float T = (i + 0.5f) / Count;
			FVector2D Inland;
			FVector2D ToSea;
			if (T < 0.55f)
			{
				const float U = T / 0.55f;
				Inland = FVector2D(-Half * 0.15f, FMath::Lerp(-Half * 0.85f, Half * 0.55f, U));
				ToSea = FVector2D(-1.f, 0.f);
			}
			else
			{
				const float U = (T - 0.55f) / 0.45f;
				Inland = FVector2D(FMath::Lerp(-Half * 0.2f, Half * 0.85f, U), Half * 0.2f);
				ToSea = FVector2D(0.f, 1.f);
			}
			const FVector2D Shore = FindCoastPoint(Inland, ToSea);
			// The shoreline wiggles, so the true "towards the sea" direction here is the coast field's
			// downhill gradient, not the straight line we marched along.
			const float E = 3000.f;
			FVector2D Seaward(CoastField(Shore - FVector2D(E, 0)) - CoastField(Shore + FVector2D(E, 0)),
				CoastField(Shore - FVector2D(0, E)) - CoastField(Shore + FVector2D(0, E)));
			Seaward = Seaward.IsNearlyZero() ? ToSea : Seaward.GetSafeNormal();
			FSpawnPoint S;
			// 40 m inland from the shoreline, on the beach, facing inland.
			S.Location = Shore - Seaward * 4000.f;
			S.Yaw = FMath::RadiansToDegrees(FMath::Atan2(-Seaward.Y, -Seaward.X));
			Layout.Spawns.Add(S);
		}
	}

	void FTerrain::FillHeightmap(int32 Verts, float ZScale, TArray<uint16>& Out) const
	{
		Out.SetNumUninitialized(Verts * Verts);
		const float Step = Settings.SizeCm / FMath::Max(Verts - 1, 1);
		ParallelFor(Verts, [&](int32 Row)
		{
			for (int32 Col = 0; Col < Verts; ++Col)
			{
				// Landscape X = world X, landscape Y = world Y, origin at the south-west (-Half, -Half) corner.
				const FVector2D P(-Half + Col * Step, -Half + Row * Step);
				Out[Row * Verts + Col] = EncodeHeight(HeightAt(P), ZScale);
			}
		});
	}

	bool FTerrain::WriteR16(const FString& Path, int32 Verts, float ZScale) const
	{
		TArray<uint16> Heights;
		FillHeightmap(Verts, ZScale, Heights);
		TArray<uint8> Bytes;
		Bytes.SetNumUninitialized(Heights.Num() * 2);
		for (int32 i = 0; i < Heights.Num(); ++i)
		{
			Bytes[i * 2] = static_cast<uint8>(Heights[i] & 0xff);
			Bytes[i * 2 + 1] = static_cast<uint8>(Heights[i] >> 8);
		}
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
		return FFileHelper::SaveArrayToFile(Bytes, *Path);
	}
}
