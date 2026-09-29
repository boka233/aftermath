#include "Combat/AftermathBallistics.h"

namespace AftermathBallistics
{
	FVector PositionAt(const FVector& Origin, const FVector& Velocity, float T)
	{
		return Origin + Velocity * T + FVector(0.f, 0.f, 0.5f * Gravity * T * T);
	}

	void BuildPath(const FVector& Origin, const FVector& Direction, float Speed, float MaxRange, float StepSeconds, TArray<FVector>& OutPoints)
	{
		OutPoints.Reset();
		const FVector Velocity = Direction.GetSafeNormal() * FMath::Max(Speed, 1.f);
		const float Step = FMath::Max(StepSeconds, 0.001f);
		// Travel time to MaxRange ignoring drop (drop barely changes path length).
		const float FlightTime = MaxRange / FMath::Max(Speed, 1.f);
		const int32 Steps = FMath::Clamp(FMath::CeilToInt(FlightTime / Step), 1, 512);

		OutPoints.Reserve(Steps + 1);
		for (int32 i = 0; i <= Steps; ++i)
		{
			OutPoints.Add(PositionAt(Origin, Velocity, FMath::Min(i * Step, FlightTime)));
		}
	}

	FVector SpreadDirection(const FVector& Aim, float HalfAngleDeg, int32 Seed)
	{
		const FVector Dir = Aim.GetSafeNormal();
		if (HalfAngleDeg <= 0.f)
		{
			return Dir;
		}
		FRandomStream Rng(Seed);
		return Rng.VRandCone(Dir, FMath::DegreesToRadians(HalfAngleDeg));
	}

	EAftermathHitZone ZoneFromHeight(float Height01, bool bProne)
	{
		const float H = FMath::Clamp(Height01, 0.f, 1.f);
		if (bProne)
		{
			// Low profile: anything that reaches the top third is the head/shoulders.
			return H > 0.66f ? EAftermathHitZone::Head : EAftermathHitZone::Torso;
		}
		if (H > 0.86f) { return EAftermathHitZone::Head; }
		if (H > 0.50f) { return EAftermathHitZone::Torso; }
		if (H > 0.42f) { return EAftermathHitZone::Arms; }   // hands/hips band
		return EAftermathHitZone::Legs;
	}

	float ZoneMultiplier(EAftermathHitZone Zone)
	{
		switch (Zone)
		{
		case EAftermathHitZone::Head:  return 3.f;
		case EAftermathHitZone::Torso: return 1.f;
		case EAftermathHitZone::Arms:  return 0.6f;
		case EAftermathHitZone::Legs:  return 0.7f;
		default:                       return 0.f;
		}
	}

	bool SegmentHitsCapsule(const FVector& A, const FVector& B, const FVector& CapsuleCenter, float Radius, float HalfHeight,
		FVector& OutHit, float& OutT)
	{
		// Capsule axis endpoints (hemisphere centres).
		const float AxisHalf = FMath::Max(0.f, HalfHeight - Radius);
		const FVector Top = CapsuleCenter + FVector(0.f, 0.f, AxisHalf);
		const FVector Bottom = CapsuleCenter - FVector(0.f, 0.f, AxisHalf);

		FVector OnSegment, OnAxis;
		FMath::SegmentDistToSegmentSafe(A, B, Bottom, Top, OnSegment, OnAxis);
		const float DistSq = FVector::DistSquared(OnSegment, OnAxis);
		if (DistSq > FMath::Square(Radius))
		{
			return false;
		}

		// Back off from the closest point to the capsule surface along the segment.
		const FVector Dir = (B - A);
		const float Len = Dir.Size();
		if (Len <= UE_KINDA_SMALL_NUMBER)
		{
			OutHit = A;
			OutT = 0.f;
			return true;
		}
		const float Penetration = FMath::Sqrt(FMath::Max(0.f, FMath::Square(Radius) - DistSq));
		const float TClosest = FVector::DotProduct(OnSegment - A, Dir) / (Len * Len);
		OutT = FMath::Clamp(TClosest - Penetration / Len, 0.f, 1.f);
		OutHit = A + Dir * OutT;
		return true;
	}

	FArmorResult ApplyArmor(float Damage, float Blood, const FAftermathArmorDef* Armor, float ArmorHealth)
	{
		FArmorResult R;
		R.Damage = FMath::Max(0.f, Damage);
		R.Blood = FMath::Max(0.f, Blood);
		if (!Armor || Armor->Absorb <= 0.f || ArmorHealth <= 0.f)
		{
			return R;
		}
		// Absorption falls off with armor health: ruined armor does nothing, pristine does Absorb.
		const float Absorb = FMath::Clamp(Armor->Absorb * FMath::Clamp(ArmorHealth, 0.f, 1.f), 0.f, 0.95f);
		const float Stopped = R.Damage * Absorb;
		R.Damage -= Stopped;
		R.Blood *= (1.f - Absorb);
		R.ArmorWear = Stopped * Armor->WearPerDamage;
		return R;
	}
}
