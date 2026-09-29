#include "Survival/AftermathSurvivalMath.h"

namespace AftermathSurvival
{
	float HeatDelta(const FAftermathSurvivalTuning& T, const FAftermathEnvironment& Env, float Insulation, float Activity, float CurrentHeat)
	{
		// Target heat the body drifts toward: ambient sets the baseline, clothes and activity push up, rain pushes down.
		const float Clothes = FMath::Clamp(Insulation, 0.f, 1.f);
		const float Exercise = FMath::Clamp((Activity - 1.f) / 3.f, 0.f, 1.f) * 0.2f;
		float Target = 0.5f + Env.AmbientWarmth * 0.4f + Clothes * 0.5f + Exercise - Env.Rain * 0.25f;
		Target = FMath::Clamp(Target, 0.f, 1.f);
		return (Target - CurrentHeat) * T.HeatRate * 10.f;
	}

	float WeightSpeedFactor(const FAftermathSurvivalTuning& T, float Kg)
	{
		if (Kg <= T.WeightComfortKg)
		{
			return 1.f;
		}
		const float Over = FMath::Clamp((Kg - T.WeightComfortKg) / FMath::Max(1.f, T.WeightMaxKg - T.WeightComfortKg), 0.f, 1.f);
		return FMath::Lerp(1.f, 0.6f, Over);
	}

	float FallDamage(const FAftermathSurvivalTuning& T, float LandingSpeed)
	{
		if (LandingSpeed <= T.FallDamageSpeed)
		{
			return 0.f;
		}
		const float Range = FMath::Max(1.f, T.FallLethalSpeed - T.FallDamageSpeed);
		const float Frac = (LandingSpeed - T.FallDamageSpeed) / Range;
		// Quadratic so short drops sting and long ones kill.
		return T.MaxHealth * FMath::Clamp(Frac * Frac * 0.7f + Frac * 0.3f, 0.f, 1.2f);
	}

	float ActivityFromSpeed(const FAftermathSurvivalTuning& T, float Speed2D)
	{
		if (Speed2D < 50.f)   { return 1.f; }
		if (Speed2D < 300.f)  { return 1.3f; }
		if (Speed2D < 520.f)  { return T.JogMetabolism; }
		return T.SprintMetabolism;
	}
}
