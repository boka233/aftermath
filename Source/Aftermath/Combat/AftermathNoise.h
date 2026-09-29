#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "AftermathNoise.generated.h"

UENUM()
enum class EAftermathNoiseKind : uint8
{
	Footstep,
	Gunshot,
	Melee,
	Cough,
	Door
};

/** One sound that the world (infected in stage 4, players' HUD) might react to. */
struct FAftermathNoiseEvent
{
	EAftermathNoiseKind Kind = EAftermathNoiseKind::Footstep;
	FVector Location = FVector::ZeroVector;
	/** Full audible radius in cm (before rain). */
	float Radius = 0.f;
	TWeakObjectPtr<AActor> Instigator;
	double Time = 0.0;
};

/** Pure loudness rules (unit-testable). */
namespace AftermathNoiseMath
{
	/** 0..1 loudness at Listener. Rain masks sound (up to 40% smaller radius). */
	AFTERMATH_API float Attenuate(const FAftermathNoiseEvent& Event, const FVector& Listener, float Rain01);

	/** Footstep radius from ground speed and stance. 0 when standing still. */
	AFTERMATH_API float FootstepRadius(float Speed2D, bool bCrouchedOrProne);
}

DECLARE_MULTICAST_DELEGATE_OneParam(FOnAftermathNoise, const FAftermathNoiseEvent&);

/**
 * Server-side registry of recent noises. Gunshots, footsteps and coughs are reported here; the
 * infected AI (stage 4) subscribes to OnNoise. Keeps a short history so late listeners can poll.
 */
UCLASS()
class AFTERMATH_API UAftermathNoiseSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	void Report(EAftermathNoiseKind Kind, const FVector& Location, float Radius, AActor* Instigator);

	/** Noises since Time that would be heard at Listener. */
	int32 CountHeardAt(const FVector& Listener, double Since, float Rain01 = 0.f) const;

	const TArray<FAftermathNoiseEvent>& GetRecent() const { return Recent; }

	FOnAftermathNoise OnNoise;

	static constexpr double KeepSeconds = 5.0;

private:
	TArray<FAftermathNoiseEvent> Recent;
};
