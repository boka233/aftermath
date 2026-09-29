#include "Combat/AftermathNoise.h"

#include "Aftermath.h"
#include "Engine/World.h"

namespace AftermathNoiseMath
{
	float Attenuate(const FAftermathNoiseEvent& Event, const FVector& Listener, float Rain01)
	{
		const float Radius = Event.Radius * FMath::Lerp(1.f, 0.6f, FMath::Clamp(Rain01, 0.f, 1.f));
		if (Radius <= 0.f)
		{
			return 0.f;
		}
		const float Dist = FVector::Dist(Event.Location, Listener);
		if (Dist >= Radius)
		{
			return 0.f;
		}
		// Linear falloff is easy to reason about for gameplay tuning.
		return 1.f - Dist / Radius;
	}

	float FootstepRadius(float Speed2D, bool bCrouchedOrProne)
	{
		if (Speed2D < 50.f)
		{
			return 0.f;
		}
		float R;
		if (Speed2D < 250.f)      { R = 600.f; }    // walking
		else if (Speed2D < 520.f) { R = 1500.f; }   // jogging
		else                      { R = 3000.f; }   // sprinting
		return bCrouchedOrProne ? R * 0.35f : R;
	}
}

bool UAftermathNoiseSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld() && World->GetNetMode() != NM_Client;
}

void UAftermathNoiseSubsystem::Report(EAftermathNoiseKind Kind, const FVector& Location, float Radius, AActor* Instigator)
{
	if (Radius <= 0.f)
	{
		return;
	}
	const UWorld* World = GetWorld();
	FAftermathNoiseEvent& E = Recent.AddDefaulted_GetRef();
	E.Kind = Kind;
	E.Location = Location;
	E.Radius = Radius;
	E.Instigator = Instigator;
	E.Time = World ? World->GetTimeSeconds() : 0.0;

	// Keep the last few seconds only; listeners poll or subscribe.
	const double Cutoff = E.Time - KeepSeconds;
	Recent.RemoveAll([Cutoff](const FAftermathNoiseEvent& N) { return N.Time < Cutoff; });

	OnNoise.Broadcast(E);
	if (Kind == EAftermathNoiseKind::Gunshot)
	{
		UE_LOG(LogAftermath, Verbose, TEXT("[Aftermath] Noise kind=gunshot radius=%.0f by=%s"), Radius, *GetNameSafe(Instigator));
	}
}

int32 UAftermathNoiseSubsystem::CountHeardAt(const FVector& Listener, double Since, float Rain01) const
{
	int32 N = 0;
	for (const FAftermathNoiseEvent& E : Recent)
	{
		if (E.Time >= Since && AftermathNoiseMath::Attenuate(E, Listener, Rain01) > 0.f)
		{
			++N;
		}
	}
	return N;
}
