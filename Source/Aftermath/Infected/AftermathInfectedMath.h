#pragma once

#include "CoreMinimal.h"
#include "Movement/AftermathMovementMath.h"

/**
 * Pure infected rules (no world access, unit-testable): type stats, vision and hearing scores,
 * the awareness/state machine, and the distance-based update rate.
 *
 * The world-facing code (AAftermathInfected, UAftermathInfectedManager) gathers inputs such as
 * line of sight or the nearest player distance, and feeds them through these functions.
 */

enum class EAftermathInfectedState : uint8
{
	Idle,     // wander near home
	Alert,    // noticed something, turn and walk toward it
	Chase,    // running at a target
	Attack,   // in reach: wind up and swing
	Search,   // lost the target, look around the last known spot
	Return,   // give up, walk back home
	Dead
};

enum class EAftermathInfectedType : uint8
{
	Civilian,
	Police,
	Soldier
};

struct FAftermathInfectedTypeDef
{
	const TCHAR* Name = TEXT("civilian");
	float Health = 100.f;
	float WalkSpeed = 150.f;
	float RunSpeed = 480.f;
	float AttackDamage = 12.f;
	float AttackBlood = 60.f;
	float AttackCutChance = 0.3f;
	/** Fraction of head damage stopped (soldier helmets). */
	float HeadArmor = 0.f;
	/** Fraction of torso damage stopped (police/soldier vests). */
	float TorsoArmor = 0.f;
	/** Placeholder body tint. */
	FLinearColor Color = FLinearColor(0.35f, 0.5f, 0.3f);
};

namespace AftermathInfectedMath
{
	// ---- Tuning (cm, seconds) ----
	constexpr float VisionRange = 3500.f;
	constexpr float VisionFovDeg = 120.f;        // full sensitivity cone (total angle)
	constexpr float PeripheralFovDeg = 200.f;    // reduced sensitivity out to here
	constexpr float AttackReach = 170.f;         // centre-to-centre distance to start a swing
	constexpr float AttackWindup = 0.45f;        // wind-up before the swing lands
	constexpr float AttackRecovery = 0.75f;      // after the swing, before the next decision (one swing per 1.2 s)
	constexpr float RoarCooldown = 10.f;
	constexpr float AlertThreshold = 0.25f;
	constexpr float ChaseThreshold = 1.f;
	constexpr float MaxAwareness = 2.f;
	constexpr float AwarenessDecayPerSec = 0.12f;
	constexpr float LoseTargetSeconds = 5.f;     // chase -> search after not seeing the target this long
	constexpr float SearchSeconds = 8.f;
	constexpr float AlertGiveUpSeconds = 10.f;
	constexpr float WanderRadius = 800.f;
	constexpr float RoarRadius = 2500.f;
	/** Walls roughly halve how far a sound carries. */
	constexpr float OcclusionFactor = 0.5f;

	AFTERMATH_API const FAftermathInfectedTypeDef& GetType(EAftermathInfectedType Type);
	AFTERMATH_API const TCHAR* StateToString(EAftermathInfectedState State);
	AFTERMATH_API const TCHAR* TypeToString(EAftermathInfectedType Type);

	struct FVisionInput
	{
		FVector Eye = FVector::ZeroVector;
		FVector Forward = FVector::ForwardVector;
		FVector Target = FVector::ZeroVector;
		EAftermathStance TargetStance = EAftermathStance::Standing;
		float TargetSpeed = 0.f;
		/** 0 = pitch dark, 1 = full daylight. */
		float Light01 = 1.f;
		bool bLineOfSight = true;
	};

	/**
	 * Awareness gained per second from seeing a target (0 if it can't be seen).
	 * Close, moving, standing targets in the middle of the view in daylight are noticed within a fraction
	 * of a second; a prone target at night at the edge of the view may never be noticed.
	 */
	AFTERMATH_API float VisionRate(const FVisionInput& In);

	/** Effective vision range for a stance/speed/light combination. */
	AFTERMATH_API float VisionRangeFor(EAftermathStance Stance, float Speed, float Light01);

	/** 0..1 loudness of a sound heard at Dist. Occluded sounds carry half as far. Rain masks up to 40%. */
	AFTERMATH_API float Hearing(float Radius, float Dist, bool bOccluded, float Rain01);

	/** Awareness bump from hearing something at this loudness. A nearby gunshot is enough to chase. */
	AFTERMATH_API float AwarenessFromSound(float Loudness);

	struct FBrainInput
	{
		EAftermathInfectedState State = EAftermathInfectedState::Idle;
		float Awareness = 0.f;
		bool bHasTarget = false;          // a live target is being tracked
		bool bTargetVisible = false;      // seen this think
		float DistToTarget = TNumericLimits<float>::Max();
		float SinceTargetSeen = 0.f;
		bool bHasInterestPoint = false;   // a heard/last-seen location to go to
		bool bReachedInterestPoint = false;
		float TimeInState = 0.f;
		bool bAtHome = true;
		bool bCalledByOthers = false;     // another infected roared with a target
	};

	/** Next state from the current inputs. Pure: the pawn applies side effects on transitions. */
	AFTERMATH_API EAftermathInfectedState NextState(const FBrainInput& In);

	/**
	 * How often to think (seconds between updates) given the distance to the nearest living player.
	 * Busy infected (chasing/attacking) always think at least at the near rate while a player is close.
	 */
	AFTERMATH_API float ThinkInterval(float NearestPlayerDist, EAftermathInfectedState State);

	/** Far from everyone and idle: stop simulating movement entirely until a player comes near. */
	AFTERMATH_API bool ShouldSleep(float NearestPlayerDist, EAftermathInfectedState State);

	/** Damage an infected takes after its type's armor. */
	AFTERMATH_API float DamageAfterArmor(const FAftermathInfectedTypeDef& Type, bool bHead, bool bTorso, float Damage);
}
