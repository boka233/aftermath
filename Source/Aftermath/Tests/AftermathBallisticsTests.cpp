// Stage 3 pure combat math: bullet drop, spread, hit zones, capsule intersection, armor, rewind buffer.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Combat/AftermathBallistics.h"
#include "Combat/AftermathHitHistory.h"
#include "Items/AftermathItemTypes.h"

namespace AftermathBallisticsTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
}

using namespace AftermathBallistics;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAftermathBulletDropTest, "Aftermath.Combat.BulletDrop", AftermathBallisticsTest::Flags)
bool FAftermathBulletDropTest::RunTest(const FString& Parameters)
{
	// 900 m/s rifle round fired level: after 0.1 s it's 90 m out and ~4.9 cm lower.
	const FVector V(90000.f, 0.f, 0.f);
	const FVector P = PositionAt(FVector::ZeroVector, V, 0.1f);
	UTEST_NEARLY_EQUAL("distance", (float)P.X, 9000.f, 0.01f);
	UTEST_NEARLY_EQUAL("drop 4.9 cm", (float)P.Z, -4.9f, 0.01f);

	// Pistol at 360 m/s: 100 m takes 0.278 s -> ~37.8 cm of drop. Slower bullets drop more over the same distance.
	const float PistolT = 10000.f / 36000.f;
	const float PistolDrop = (float)-PositionAt(FVector::ZeroVector, FVector(36000.f, 0, 0), PistolT).Z;
	const float RifleT = 10000.f / 88000.f;
	const float RifleDrop = (float)-PositionAt(FVector::ZeroVector, FVector(88000.f, 0, 0), RifleT).Z;
	UTEST_NEARLY_EQUAL("pistol drop at 100 m", PistolDrop, 37.8f, 0.5f);
	UTEST_TRUE("rifle drops less", RifleDrop < PistolDrop * 0.25f);

	// Path segments: start at the muzzle, end at MaxRange, monotonic in X, all falling.
	TArray<FVector> Path;
	BuildPath(FVector(0, 0, 150.f), FVector::ForwardVector, 36000.f, 15000.f, 0.02f, Path);
	UTEST_TRUE("several segments", Path.Num() > 5);
	UTEST_TRUE("starts at muzzle", Path[0].Equals(FVector(0, 0, 150.f), 0.01f));
	UTEST_NEARLY_EQUAL("reaches max range", (float)Path.Last().X, 15000.f, 1.f);
	for (int32 i = 1; i < Path.Num(); ++i)
	{
		UTEST_TRUE("forward", Path[i].X > Path[i - 1].X - 0.01f);
		UTEST_TRUE("falling", Path[i].Z <= Path[i - 1].Z + 0.01f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAftermathSpreadTest, "Aftermath.Combat.SpreadIsDeterministicAndBounded", AftermathBallisticsTest::Flags)
bool FAftermathSpreadTest::RunTest(const FString& Parameters)
{
	const FVector Aim(1.f, 0.f, 0.f);
	UTEST_TRUE("zero spread = aim", SpreadDirection(Aim, 0.f, 7).Equals(Aim, 1e-4f));
	UTEST_TRUE("same seed same shot", SpreadDirection(Aim, 2.f, 42).Equals(SpreadDirection(Aim, 2.f, 42), 1e-6f));
	UTEST_TRUE("different seed different shot", !SpreadDirection(Aim, 2.f, 42).Equals(SpreadDirection(Aim, 2.f, 43), 1e-6f));
	float MaxAngle = 0.f;
	for (int32 Seed = 0; Seed < 500; ++Seed)
	{
		const FVector D = SpreadDirection(Aim, 2.f, Seed);
		UTEST_NEARLY_EQUAL("unit length", (float)D.Size(), 1.f, 1e-4f);
		MaxAngle = FMath::Max(MaxAngle, FMath::RadiansToDegrees(FMath::Acos(FVector::DotProduct(D, Aim))));
	}
	UTEST_TRUE("inside the cone", MaxAngle <= 2.001f);
	UTEST_TRUE("uses the cone", MaxAngle > 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAftermathHitZoneTest, "Aftermath.Combat.HitZonesAndArmor", AftermathBallisticsTest::Flags)
bool FAftermathHitZoneTest::RunTest(const FString& Parameters)
{
	UTEST_EQUAL("feet", ZoneFromHeight(0.05f, false), EAftermathHitZone::Legs);
	UTEST_EQUAL("knees", ZoneFromHeight(0.3f, false), EAftermathHitZone::Legs);
	UTEST_EQUAL("hands", ZoneFromHeight(0.45f, false), EAftermathHitZone::Arms);
	UTEST_EQUAL("chest", ZoneFromHeight(0.7f, false), EAftermathHitZone::Torso);
	UTEST_EQUAL("head", ZoneFromHeight(0.93f, false), EAftermathHitZone::Head);
	UTEST_EQUAL("prone low", ZoneFromHeight(0.3f, true), EAftermathHitZone::Torso);
	UTEST_EQUAL("prone top", ZoneFromHeight(0.8f, true), EAftermathHitZone::Head);
	UTEST_TRUE("headshots hurt most", ZoneMultiplier(EAftermathHitZone::Head) > ZoneMultiplier(EAftermathHitZone::Torso));
	UTEST_TRUE("limbs hurt less", ZoneMultiplier(EAftermathHitZone::Legs) < 1.f && ZoneMultiplier(EAftermathHitZone::Arms) < 1.f);

	FAftermathArmorDef Plate;
	Plate.Zone = EAftermathHitZone::Torso;
	Plate.Absorb = 0.5f;
	Plate.WearPerDamage = 0.01f;
	const FArmorResult None = ApplyArmor(60.f, 800.f, nullptr, 1.f);
	UTEST_TRUE("no armor passes all", None.Damage == 60.f && None.Blood == 800.f && None.ArmorWear == 0.f);
	const FArmorResult Full = ApplyArmor(60.f, 800.f, &Plate, 1.f);
	UTEST_NEARLY_EQUAL("half stopped", Full.Damage, 30.f, 0.01f);
	UTEST_NEARLY_EQUAL("half the bleeding", Full.Blood, 400.f, 0.01f);
	UTEST_NEARLY_EQUAL("armor wears", Full.ArmorWear, 0.3f, 0.001f);
	const FArmorResult Worn = ApplyArmor(60.f, 800.f, &Plate, 0.5f);
	UTEST_TRUE("worn armor stops less", Worn.Damage > Full.Damage);
	const FArmorResult Ruined = ApplyArmor(60.f, 800.f, &Plate, 0.f);
	UTEST_EQUAL("ruined armor stops nothing", Ruined.Damage, 60.f);

	UTEST_NEARLY_EQUAL("600 rpm = 0.1 s", ShotInterval(600.f), 0.1f, 1e-5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAftermathCapsuleHitTest, "Aftermath.Combat.SegmentVsCapsule", AftermathBallisticsTest::Flags)
bool FAftermathCapsuleHitTest::RunTest(const FString& Parameters)
{
	// Standing capsule at (1000,0,90): radius 35, half height 90 (feet at z=0, top at z=180).
	const FVector C(1000.f, 0.f, 90.f);
	FVector Hit;
	float T = 0.f;

	UTEST_TRUE("chest shot hits", SegmentHitsCapsule(FVector(0, 0, 120), FVector(2000, 0, 120), C, 35.f, 90.f, Hit, T));
	UTEST_NEARLY_EQUAL("enters at the front surface", (float)Hit.X, 965.f, 1.f);
	UTEST_NEARLY_EQUAL("t", T, 965.f / 2000.f, 0.001f);

	UTEST_TRUE("just misses to the side", !SegmentHitsCapsule(FVector(0, 40, 120), FVector(2000, 40, 120), C, 35.f, 90.f, Hit, T));
	UTEST_TRUE("grazes within radius", SegmentHitsCapsule(FVector(0, 30, 120), FVector(2000, 30, 120), C, 35.f, 90.f, Hit, T));
	UTEST_TRUE("over the head misses", !SegmentHitsCapsule(FVector(0, 0, 200), FVector(2000, 0, 200), C, 35.f, 90.f, Hit, T));
	UTEST_TRUE("top hemisphere hits", SegmentHitsCapsule(FVector(0, 0, 170), FVector(2000, 0, 170), C, 35.f, 90.f, Hit, T));
	UTEST_TRUE("stops short misses", !SegmentHitsCapsule(FVector(0, 0, 120), FVector(900, 0, 120), C, 35.f, 90.f, Hit, T));
	UTEST_TRUE("starts inside hits at t=0", SegmentHitsCapsule(FVector(1000, 0, 100), FVector(3000, 0, 100), C, 35.f, 90.f, Hit, T) && T == 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAftermathPoseBufferTest, "Aftermath.Combat.RewindBuffer", AftermathBallisticsTest::Flags)
bool FAftermathPoseBufferTest::RunTest(const FString& Parameters)
{
	FAftermathPoseBuffer B;
	B.MaxAgeSeconds = 1.0;
	FAftermathPoseSample Out;
	UTEST_TRUE("empty buffer", !B.Sample(0.0, Out));

	// Target runs +X at 5 m/s, sampled at 30 Hz.
	for (int32 i = 0; i <= 60; ++i)
	{
		FAftermathPoseSample S;
		S.Time = i / 30.0;
		S.Location = FVector(500.f * S.Time, 0.f, 90.f);
		S.bProne = i >= 45;
		B.Record(S);
	}
	UTEST_TRUE("trimmed to ~1 s", B.Samples.Num() <= 32);
	UTEST_TRUE("oldest within window", B.Samples[0].Time >= 2.0 - 1.0 - 1.0 / 30.0 - 1e-6);

	UTEST_TRUE("interpolates", B.Sample(1.51, Out));
	UTEST_NEARLY_EQUAL("position between samples", (float)Out.Location.X, 755.f, 0.5f);
	UTEST_TRUE("stance switches at the right sample", B.Sample(1.52, Out) && Out.bProne == (1.52 * 30.0 >= 44.5));

	UTEST_TRUE("future clamps to latest", B.Sample(5.0, Out) && FMath::IsNearlyEqual((float)Out.Location.X, 1000.f, 0.01f));
	UTEST_TRUE("slightly before oldest ok", B.Sample(B.Samples[0].Time - 0.1, Out));
	UTEST_TRUE("far past refused", !B.Sample(-10.0, Out));

	// Out-of-order samples are ignored.
	const int32 N = B.Samples.Num();
	FAftermathPoseSample Old;
	Old.Time = 0.5;
	B.Record(Old);
	UTEST_EQUAL("ignored", B.Samples.Num(), N);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
