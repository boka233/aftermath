#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "AftermathHitHistory.generated.h"

class AAftermathCharacter;

/** Where a character's body was at one moment (server time). */
struct FAftermathPoseSample
{
	double Time = 0.0;
	FVector Location = FVector::ZeroVector;
	float HalfHeight = 90.f;
	float Radius = 35.f;
	bool bProne = false;
};

/**
 * Time-ordered ring of pose samples for one character. Pure data (unit-testable):
 * Record() appends and trims to MaxAgeSeconds, Sample() interpolates between neighbours.
 */
struct AFTERMATH_API FAftermathPoseBuffer
{
	double MaxAgeSeconds = 1.0;
	TArray<FAftermathPoseSample> Samples;

	void Record(const FAftermathPoseSample& Sample);

	/** Pose at Time: interpolated inside the window, clamped to the ends outside. False if empty or far too old. */
	bool Sample(double Time, FAftermathPoseSample& Out) const;
};

/**
 * Server-only lag compensation. Every tick it records each survivor's capsule. When a shot arrives,
 * the combat code asks where targets were at the moment the shooter pulled the trigger (their view
 * of the world was ~ping/2 + interpolation behind the server) and tests the bullet against that.
 *
 * Rewind is capped (MaxRewindSeconds) so a laggy or lying client can't shoot into the distant past.
 */
UCLASS()
class AFTERMATH_API UAftermathHitHistory : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** Record everyone now (also called by Tick). Tests call it directly. */
	void RecordAll();

	/** Survivors and infected (anything a bullet can hit). */
	bool GetPoseAt(const AActor* Who, double Time, FAftermathPoseSample& Out) const;

	/** Server time now. */
	double Now() const;

	/** Clamp a client-claimed shot time into the allowed rewind window. */
	double ClampRewind(double RequestedTime) const;

	/** Max rewind (seconds). 250 ms covers ~200 ms ping plus interpolation delay. */
	static constexpr double MaxRewindSeconds = 0.25;

private:
	TMap<TWeakObjectPtr<AActor>, FAftermathPoseBuffer> Buffers;
};
