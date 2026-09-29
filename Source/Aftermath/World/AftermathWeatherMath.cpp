#include "World/AftermathWeatherMath.h"

namespace AftermathWeather
{
	float HourAt(const FConfig& C, double ElapsedSeconds)
	{
		const double HoursPerSecond = 24.0 / FMath::Max(1.0, static_cast<double>(C.DaySeconds));
		const double H = FMath::Fmod(C.StartHour + ElapsedSeconds * HoursPerSecond, 24.0);
		return static_cast<float>(H < 0.0 ? H + 24.0 : H);
	}

	float Daylight(const FConfig& C, float Hour)
	{
		const float Twilight = 1.f;
		if (Hour >= C.SunriseHour + Twilight && Hour <= C.SunsetHour - Twilight)
		{
			return 1.f;
		}
		if (Hour <= C.SunriseHour - Twilight || Hour >= C.SunsetHour + Twilight)
		{
			return 0.f;
		}
		// Twilight ramps (1 h either side of sunrise / sunset).
		if (Hour < 12.f)
		{
			return FMath::SmoothStep(C.SunriseHour - Twilight, C.SunriseHour + Twilight, Hour);
		}
		return 1.f - FMath::SmoothStep(C.SunsetHour - Twilight, C.SunsetHour + Twilight, Hour);
	}

	float SunPitch(const FConfig& C, float Hour)
	{
		// Map the day (sunrise..sunset) onto 0..180 degrees of elevation; the night half goes below.
		const float Day = C.SunsetHour - C.SunriseHour;
		float Phase;
		if (Hour >= C.SunriseHour && Hour <= C.SunsetHour)
		{
			Phase = (Hour - C.SunriseHour) / Day * 180.f;                    // 0 at sunrise, 90 noon, 180 sunset
		}
		else
		{
			const float Night = 24.f - Day;
			const float SinceSunset = FMath::Fmod(Hour - C.SunsetHour + 24.f, 24.f);
			Phase = 180.f + SinceSunset / Night * 180.f;                     // 180..360 under the horizon
		}
		// Elevation peaks at 60 degrees (a temperate latitude), light pitch is negative when above.
		const float Elevation = FMath::Sin(FMath::DegreesToRadians(Phase)) * 60.f;
		return -Elevation;
	}

	float AmbientWarmth(float Daylight01, float Rain01)
	{
		return FMath::Lerp(-0.5f, 0.1f, FMath::Clamp(Daylight01, 0.f, 1.f)) - 0.3f * FMath::Clamp(Rain01, 0.f, 1.f);
	}

	float NextRainTarget(float Random01)
	{
		if (Random01 < 0.6f) { return 0.f; }      // dry
		if (Random01 < 0.9f) { return 0.35f; }    // drizzle
		return 1.f;                               // storm
	}

	float BlendToward(float Current, float Target, float Rate, float Dt)
	{
		const float Step = Rate * Dt;
		return Current < Target ? FMath::Min(Current + Step, Target) : FMath::Max(Current - Step, Target);
	}
}
