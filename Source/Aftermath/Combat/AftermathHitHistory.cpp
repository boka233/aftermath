#include "Combat/AftermathHitHistory.h"

#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Movement/AftermathMovementComponent.h"
#include "Player/AftermathCharacter.h"

// ---------------------------------------------------------------------------
// Pure buffer
// ---------------------------------------------------------------------------

void FAftermathPoseBuffer::Record(const FAftermathPoseSample& Sample)
{
	// Keep time order; drop out-of-order samples.
	if (Samples.Num() > 0 && Sample.Time <= Samples.Last().Time)
	{
		return;
	}
	Samples.Add(Sample);
	// Trim anything older than the window.
	const double Cutoff = Sample.Time - MaxAgeSeconds;
	int32 FirstKept = 0;
	while (FirstKept < Samples.Num() - 1 && Samples[FirstKept + 1].Time < Cutoff)
	{
		++FirstKept;
	}
	if (FirstKept > 0)
	{
		Samples.RemoveAt(0, FirstKept, EAllowShrinking::No);
	}
}

bool FAftermathPoseBuffer::Sample(double Time, FAftermathPoseSample& Out) const
{
	if (Samples.Num() == 0)
	{
		return false;
	}
	if (Time <= Samples[0].Time)
	{
		Out = Samples[0];
		return Time >= Samples[0].Time - MaxAgeSeconds * 0.5; // a bit before the oldest is still usable
	}
	if (Time >= Samples.Last().Time)
	{
		Out = Samples.Last();
		return true;
	}
	// Binary search for the bracketing pair.
	int32 Lo = 0;
	int32 Hi = Samples.Num() - 1;
	while (Hi - Lo > 1)
	{
		const int32 Mid = (Lo + Hi) / 2;
		if (Samples[Mid].Time <= Time) { Lo = Mid; } else { Hi = Mid; }
	}
	const FAftermathPoseSample& A = Samples[Lo];
	const FAftermathPoseSample& B = Samples[Hi];
	const float Alpha = static_cast<float>((Time - A.Time) / FMath::Max(B.Time - A.Time, 1e-6));
	Out.Time = Time;
	Out.Location = FMath::Lerp(A.Location, B.Location, Alpha);
	Out.HalfHeight = FMath::Lerp(A.HalfHeight, B.HalfHeight, Alpha);
	Out.Radius = FMath::Lerp(A.Radius, B.Radius, Alpha);
	Out.bProne = Alpha < 0.5f ? A.bProne : B.bProne;
	return true;
}

// ---------------------------------------------------------------------------
// Subsystem
// ---------------------------------------------------------------------------

bool UAftermathHitHistory::ShouldCreateSubsystem(UObject* Outer) const
{
	// Only authoritative worlds rewind; clients never need it.
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld() && World->GetNetMode() != NM_Client;
}

TStatId UAftermathHitHistory::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UAftermathHitHistory, STATGROUP_Tickables);
}

double UAftermathHitHistory::Now() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetTimeSeconds() : 0.0;
}

void UAftermathHitHistory::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	RecordAll();
}

void UAftermathHitHistory::RecordAll()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const double T = Now();
	for (TActorIterator<AAftermathCharacter> It(World); It; ++It)
	{
		AAftermathCharacter* C = *It;
		if (!IsValid(C) || C->IsDead())
		{
			continue;
		}
		const UCapsuleComponent* Capsule = C->GetCapsuleComponent();
		FAftermathPoseSample S;
		S.Time = T;
		S.Location = C->GetActorLocation();
		S.HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
		S.Radius = Capsule->GetScaledCapsuleRadius();
		S.bProne = C->GetStance() == EAftermathStance::Prone;
		Buffers.FindOrAdd(C).Record(S);
	}
	// Forget destroyed characters.
	for (auto It = Buffers.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}
}

bool UAftermathHitHistory::GetPoseAt(const AAftermathCharacter* Who, double Time, FAftermathPoseSample& Out) const
{
	const FAftermathPoseBuffer* B = Buffers.Find(Who);
	return B && B->Sample(Time, Out);
}

double UAftermathHitHistory::ClampRewind(double RequestedTime) const
{
	const double T = Now();
	return FMath::Clamp(RequestedTime, T - MaxRewindSeconds, T);
}
