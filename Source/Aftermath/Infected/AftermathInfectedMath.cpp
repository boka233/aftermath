#include "Infected/AftermathInfectedMath.h"

namespace AftermathInfectedMath
{
	const FAftermathInfectedTypeDef& GetType(EAftermathInfectedType Type)
	{
		static const FAftermathInfectedTypeDef Civilian = [] {
			FAftermathInfectedTypeDef D;
			D.Name = TEXT("civilian");
			return D;
		}();
		static const FAftermathInfectedTypeDef Police = [] {
			FAftermathInfectedTypeDef D;
			D.Name = TEXT("police");
			D.Health = 130.f;
			D.AttackDamage = 14.f;
			D.TorsoArmor = 0.4f;
			D.Color = FLinearColor(0.15f, 0.2f, 0.45f);
			return D;
		}();
		static const FAftermathInfectedTypeDef Soldier = [] {
			FAftermathInfectedTypeDef D;
			D.Name = TEXT("soldier");
			D.Health = 170.f;
			D.RunSpeed = 500.f;
			D.AttackDamage = 16.f;
			D.HeadArmor = 0.5f;
			D.TorsoArmor = 0.5f;
			D.Color = FLinearColor(0.3f, 0.35f, 0.15f);
			return D;
		}();
		switch (Type)
		{
		case EAftermathInfectedType::Police:  return Police;
		case EAftermathInfectedType::Soldier: return Soldier;
		default:                              return Civilian;
		}
	}

	const TCHAR* StateToString(EAftermathInfectedState State)
	{
		switch (State)
		{
		case EAftermathInfectedState::Idle:   return TEXT("idle");
		case EAftermathInfectedState::Alert:  return TEXT("alert");
		case EAftermathInfectedState::Chase:  return TEXT("chase");
		case EAftermathInfectedState::Attack: return TEXT("attack");
		case EAftermathInfectedState::Search: return TEXT("search");
		case EAftermathInfectedState::Return: return TEXT("return");
		case EAftermathInfectedState::Dead:   return TEXT("dead");
		default:                              return TEXT("?");
		}
	}

	const TCHAR* TypeToString(EAftermathInfectedType Type)
	{
		return GetType(Type).Name;
	}

	float VisionRangeFor(EAftermathStance Stance, float Speed, float Light01)
	{
		float Range = VisionRange;
		switch (Stance)
		{
		case EAftermathStance::Crouched: Range *= 0.55f; break;
		case EAftermathStance::Prone:    Range *= 0.25f; break;
		default: break;
		}
		// Movement catches the eye: sprinting doubles how far you're noticed (capped at the base range * 1.3).
		const float Move01 = FMath::Clamp(Speed / 600.f, 0.f, 1.f);
		Range *= FMath::Lerp(0.8f, 1.3f, Move01);
		// Night: 30% of daylight range at full dark.
		Range *= FMath::Lerp(0.3f, 1.f, FMath::Clamp(Light01, 0.f, 1.f));
		return Range;
	}

	float VisionRate(const FVisionInput& In)
	{
		if (!In.bLineOfSight)
		{
			return 0.f;
		}
		const FVector ToTarget = In.Target - In.Eye;
		const float Dist = ToTarget.Size();
		const float Range = VisionRangeFor(In.TargetStance, In.TargetSpeed, In.Light01);
		if (Dist >= Range)
		{
			return 0.f;
		}
		// Angle from the view direction, flattened (infected don't look up much).
		const FVector Flat = FVector(ToTarget.X, ToTarget.Y, 0.f).GetSafeNormal();
		const FVector Fwd = FVector(In.Forward.X, In.Forward.Y, 0.f).GetSafeNormal();
		const float AngleDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Flat, Fwd), -1.f, 1.f)));
		float AngleFactor;
		if (Dist < 150.f)
		{
			AngleFactor = 1.f; // someone touching you is noticed whatever the direction
		}
		else if (AngleDeg <= VisionFovDeg * 0.5f)
		{
			AngleFactor = 1.f;
		}
		else if (AngleDeg <= PeripheralFovDeg * 0.5f)
		{
			AngleFactor = 0.3f;
		}
		else
		{
			return 0.f;
		}
		// Closer = much faster. At 20% of range: ~4/s (instant chase); at the edge: ~0.
		const float Near01 = 1.f - Dist / Range;
		return 4.f * Near01 * Near01 * AngleFactor + (Near01 > 0.f ? 0.15f * AngleFactor : 0.f);
	}

	float Hearing(float Radius, float Dist, bool bOccluded, float Rain01)
	{
		float R = Radius * FMath::Lerp(1.f, 0.6f, FMath::Clamp(Rain01, 0.f, 1.f));
		if (bOccluded)
		{
			R *= OcclusionFactor;
		}
		if (R <= 0.f || Dist >= R)
		{
			return 0.f;
		}
		return 1.f - Dist / R;
	}

	float AwarenessFromSound(float Loudness)
	{
		if (Loudness <= 0.f)
		{
			return 0.f;
		}
		// Faint sounds only make them look; loud ones (> 0.5) send them straight into a chase toward the source.
		return Loudness * 2.2f;
	}

	EAftermathInfectedState NextState(const FBrainInput& In)
	{
		using S = EAftermathInfectedState;
		switch (In.State)
		{
		case S::Dead:
			return S::Dead;

		case S::Idle:
		case S::Return:
			if (In.bHasTarget && In.bTargetVisible && In.Awareness >= ChaseThreshold)
			{
				return S::Chase;
			}
			if (In.bCalledByOthers)
			{
				return S::Alert;
			}
			if (In.Awareness >= AlertThreshold && In.bHasInterestPoint)
			{
				return S::Alert;
			}
			if (In.State == S::Return && In.bAtHome)
			{
				return S::Idle;
			}
			return In.State;

		case S::Alert:
			if (In.bHasTarget && In.bTargetVisible && In.Awareness >= ChaseThreshold)
			{
				return S::Chase;
			}
			// Walked to the noise and found nothing, or waited too long.
			if (In.bReachedInterestPoint || !In.bHasInterestPoint)
			{
				return S::Search;
			}
			if (In.TimeInState > AlertGiveUpSeconds && In.Awareness < AlertThreshold)
			{
				return S::Return;
			}
			return S::Alert;

		case S::Chase:
			if (!In.bHasTarget)
			{
				return S::Search;
			}
			if (In.DistToTarget <= AttackReach)
			{
				return S::Attack;
			}
			if (!In.bTargetVisible && In.SinceTargetSeen > LoseTargetSeconds)
			{
				return S::Search;
			}
			return S::Chase;

		case S::Attack:
			// A swing plays out fully (windup + cooldown) before deciding again.
			if (In.TimeInState < AttackWindup + AttackRecovery)
			{
				return S::Attack;
			}
			if (!In.bHasTarget)
			{
				return S::Search;
			}
			return In.DistToTarget <= AttackReach ? S::Attack : S::Chase;

		case S::Search:
			if (In.bHasTarget && In.bTargetVisible && In.Awareness >= ChaseThreshold)
			{
				return S::Chase;
			}
			if (In.TimeInState > SearchSeconds)
			{
				return S::Return;
			}
			return S::Search;
		}
		return In.State;
	}

	float ThinkInterval(float NearestPlayerDist, EAftermathInfectedState State)
	{
		const bool bBusy = State == EAftermathInfectedState::Chase || State == EAftermathInfectedState::Attack;
		if (NearestPlayerDist < 3000.f || bBusy)
		{
			return 0.1f;   // 10 Hz: close enough that reactions must feel immediate
		}
		if (NearestPlayerDist < 8000.f)
		{
			return 0.3f;
		}
		if (NearestPlayerDist < 20000.f)
		{
			return 1.f;
		}
		return 2.f;
	}

	bool ShouldSleep(float NearestPlayerDist, EAftermathInfectedState State)
	{
		return NearestPlayerDist > 20000.f && (State == EAftermathInfectedState::Idle || State == EAftermathInfectedState::Return);
	}

	float DamageAfterArmor(const FAftermathInfectedTypeDef& Type, bool bHead, bool bTorso, float Damage)
	{
		if (bHead)
		{
			return Damage * (1.f - Type.HeadArmor);
		}
		if (bTorso)
		{
			return Damage * (1.f - Type.TorsoArmor);
		}
		return Damage;
	}
}
