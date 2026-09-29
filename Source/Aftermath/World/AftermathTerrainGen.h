#pragma once

#include "CoreMinimal.h"

/**
 * Pure world-generation rules for the 10 x 10 km map (no engine world access, unit-testable).
 *
 * Coordinates are world centimetres with the map centred on the origin: X in [-Half, Half] (north = +X),
 * Y in [-Half, Half] (east = +Y). Heights are centimetres above sea level (sea level = 0).
 *
 * Shape (DayZ-like Eastern European hills): the sea lies to the south and east; the land rises toward
 * a north-west plateau. The coastline wanders (bays, one peninsula). Rolling hills come from fractal
 * value noise; a few valleys cut toward the sea. Settlements sit on flattened pads, joined by roads.
 */
namespace AftermathTerrainGen
{
	enum class ESiteKind : uint8
	{
		City,       // harbour city on the coast
		Town,
		Village,
		Airfield,   // military
		Base,       // military
		Farm
	};

	struct FSite
	{
		ESiteKind Kind = ESiteKind::Village;
		FString Name;
		FVector2D Center = FVector2D::ZeroVector;   // cm
		float Radius = 0.f;                         // flattened area radius, cm
		float PadHeight = 0.f;                      // height of the flat pad, cm
	};

	struct FRoad
	{
		int32 From = INDEX_NONE;   // site indices
		int32 To = INDEX_NONE;
		TArray<FVector2D> Points;  // polyline, cm
	};

	struct FSpawnPoint
	{
		FVector2D Location = FVector2D::ZeroVector;
		float Yaw = 0.f;          // facing inland
	};

	struct FSettings
	{
		int32 Seed = 1337;
		float SizeCm = 1000000.f;          // 10 km
		float MaxHeightCm = 38000.f;       // 380 m hill tops on the NW plateau
		float SeaDepthCm = 3000.f;         // sea floor ~30 m below sea level far offshore
		float BeachWidthCm = 12000.f;      // 120 m from shoreline to the first hills
		/** Distance from the south/east edge that is open sea (before the coastline noise). */
		float SeaBandCm = 150000.f;
	};

	struct FLayout
	{
		TArray<FSite> Sites;
		TArray<FRoad> Roads;
		TArray<FSpawnPoint> Spawns;
	};

	/**
	 * Heights for the whole map. Build once, then sample. Sites flatten the ground around them.
	 */
	class AFTERMATH_API FTerrain
	{
	public:
		explicit FTerrain(const FSettings& InSettings);

		const FSettings& GetSettings() const { return Settings; }
		const FLayout& GetLayout() const { return Layout; }

		/** Height at a world XY (cm above sea level; negative = under water). */
		float HeightAt(const FVector2D& P) const;

		/** Natural ground before settlements and roads flattened it. */
		float NaturalHeightAt(const FVector2D& P) const;

		/** Signed "land-ness": > 0 on land, < 0 at sea, ~0 on the shoreline (cm-ish scale). */
		float CoastField(const FVector2D& P) const;

		bool IsLand(const FVector2D& P) const { return HeightAt(P) > 50.f; }

		/** Rough slope (height change per metre) at P. */
		float SlopeAt(const FVector2D& P) const;

		/**
		 * Fill a Verts x Verts heightmap in landscape uint16 encoding for a landscape that spans the whole
		 * map: value = 32768 + height_cm / ZScale * 128. Row-major, row = Y index (landscape Y), col = X.
		 */
		void FillHeightmap(int32 Verts, float ZScale, TArray<uint16>& Out) const;

		/** Write the heightmap as a little-endian .r16 file. */
		bool WriteR16(const FString& Path, int32 Verts, float ZScale) const;

		/** Distance (cm) from P to the nearest road centre line. */
		float DistanceToRoad(const FVector2D& P) const;

	private:
		void BuildLayout();
		void PlaceSites();
		void PlaceRoads();
		void PlaceSpawns();
		FVector2D FindCoastPoint(const FVector2D& From, const FVector2D& Dir) const;
		FVector2D FindLandSpot(const FVector2D& Near, float SearchRadius, float MinHeight, float MaxHeight, int32 Salt) const;

		FSettings Settings;
		FLayout Layout;
		float Half = 0.f;
	};

	/** Deterministic fractal value noise in [-1, 1]. */
	AFTERMATH_API float FractalNoise(const FVector2D& P, float Frequency, int32 Octaves, int32 Seed);

	/** Distance from P to segment AB. */
	AFTERMATH_API float DistToSegment(const FVector2D& P, const FVector2D& A, const FVector2D& B);

	AFTERMATH_API const TCHAR* SiteKindToString(ESiteKind Kind);

	/** Landscape uint16 encoding for a height in cm given the landscape Z scale. */
	AFTERMATH_API uint16 EncodeHeight(float HeightCm, float ZScale);
	AFTERMATH_API float DecodeHeight(uint16 Value, float ZScale);
}
