#pragma once

#include "CoreMinimal.h"
#include "Items/AftermathItemTypes.h"

/**
 * Pure combat math, no world access: bullet flight, hit zones, damage after armor.
 * Server hit validation and client tracers both use these, so they agree on where a bullet goes.
 *
 * Units: cm, seconds. Gravity is the engine default (-980 cm/s^2).
 */
namespace AftermathBallistics
{
	constexpr float Gravity = -980.f;

	/** Bullet position after T seconds (no drag; drop matters at range, drag isn't worth the cost here). */
	AFTERMATH_API FVector PositionAt(const FVector& Origin, const FVector& Velocity, float T);

	/**
	 * Split the flight into straight segments no longer than StepSeconds, up to MaxRange of travel.
	 * Tracing each segment approximates the arc; 0.02s at 900 m/s is 18 m per segment.
	 */
	AFTERMATH_API void BuildPath(const FVector& Origin, const FVector& Direction, float Speed, float MaxRange,
		float StepSeconds, TArray<FVector>& OutPoints);

	/** Direction with a random offset inside a cone of HalfAngleDeg, from a deterministic seed. */
	AFTERMATH_API FVector SpreadDirection(const FVector& Aim, float HalfAngleDeg, int32 Seed);

	/**
	 * Hit zone from the impact height on a standing/crouched/prone capsule (0 = feet, 1 = top).
	 * Prone bodies are horizontal: a prone target is hit mostly in the torso/head from the front.
	 */
	AFTERMATH_API EAftermathHitZone ZoneFromHeight(float Height01, bool bProne);

	/** Damage multiplier per zone. */
	AFTERMATH_API float ZoneMultiplier(EAftermathHitZone Zone);

	/**
	 * Closest-approach test between a segment and a vertical capsule (centre, radius, half height).
	 * Returns true if the segment passes within Radius of the capsule's axis segment. OutHit is the
	 * approximate entry point, OutT the fraction along the segment.
	 */
	AFTERMATH_API bool SegmentHitsCapsule(const FVector& A, const FVector& B, const FVector& CapsuleCenter,
		float Radius, float HalfHeight, FVector& OutHit, float& OutT);

	/** Result of applying armor to a hit. */
	struct FArmorResult
	{
		float Damage = 0.f;
		float Blood = 0.f;
		/** Armor health (0..1) lost. */
		float ArmorWear = 0.f;
	};

	/** Damage after armor. ArmorHealth scales absorption (worn armor stops less). */
	AFTERMATH_API FArmorResult ApplyArmor(float Damage, float Blood, const FAftermathArmorDef* Armor, float ArmorHealth);

	/** Seconds between shots for a rate of fire. */
	inline float ShotInterval(float Rpm) { return Rpm > 0.f ? 60.f / Rpm : 1.f; }

	/** Muzzle position is plausible if within this distance of the shooter's eye (server anti-cheat). */
	constexpr float MaxMuzzleOffset = 150.f;

	/** Server accepts an aim direction if it's within this many degrees of the replicated view. */
	constexpr float MaxAimErrorDeg = 25.f;
}
