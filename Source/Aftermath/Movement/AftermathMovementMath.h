#pragma once

#include "CoreMinimal.h"
#include "AftermathMovementMath.generated.h"

/** Physical stance. Derived from the real capsule, so it can differ from what the player asked for (e.g. blocked stand-up). */
UENUM(BlueprintType)
enum class EAftermathStance : uint8
{
	Standing,
	Crouched,
	Prone
};

/** Ground speeds in cm/s. */
USTRUCT(BlueprintType)
struct FAftermathSpeedConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Speed")
	float Jog = 450.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Speed")
	float Walk = 180.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Speed")
	float Sprint = 650.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Speed")
	float Crouch = 200.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Speed")
	float CrouchWalk = 120.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Speed")
	float Prone = 90.f;
};

/**
 * Vault trajectory: rise to the peak, cross the obstacle at the peak, drop to the landing point.
 * Distances are measured horizontally along Forward from Start (capsule centre).
 * Pure data + math so client prediction and server simulation compute the exact same path.
 */
struct AFTERMATH_API FAftermathVaultPath
{
	FVector Start = FVector::ZeroVector;
	FVector Forward = FVector::ForwardVector;

	/** Horizontal distance at which the capsule reaches peak height (just before touching the wall). */
	float RiseEnd = 0.f;
	/** Horizontal distance at which the capsule has fully cleared the wall's back face. */
	float HoldEnd = 0.f;
	/** Horizontal distance to the landing point. */
	float Length = 0.f;

	float StartZ = 0.f;
	float PeakZ = 0.f;
	float EndZ = 0.f;

	bool IsValid() const { return Length > UE_KINDA_SMALL_NUMBER && HoldEnd >= RiseEnd && Length >= HoldEnd; }

	/** Total length of the 3-segment polyline. */
	float TotalPathLength() const;

	/** Capsule centre after travelling Traveled cm along the path (clamped to the end). */
	FVector Evaluate(float Traveled) const;

	/** Horizontal distance from Start where the landing capsule should be placed. */
	static float LandingDistance(float WallBackDist, float CapsuleRadius) { return WallBackDist + CapsuleRadius + 40.f; }

	static FAftermathVaultPath Build(const FVector& StartCenter, const FVector& Forward,
		float WallFrontDist, float WallBackDist, float WallTopZ, float EndCenterZ,
		float CapsuleRadius, float CapsuleHalfHeight, float Clearance);
};

namespace AftermathMath
{
	/** Max ground speed for a stance and movement intent. Sprint only applies standing and moving forward. */
	AFTERMATH_API float ComputeMaxGroundSpeed(const FAftermathSpeedConfig& Config, EAftermathStance Stance,
		bool bWantsSprint, bool bWantsWalk, bool bMovingForward);

	/** Server-side reach check: target point within Range (+Tolerance for latency) of the eye. */
	AFTERMATH_API bool IsWithinInteractRange(const FVector& Eye, const FVector& Target, float Range, float Tolerance);

	AFTERMATH_API const TCHAR* StanceToString(EAftermathStance Stance);
}
