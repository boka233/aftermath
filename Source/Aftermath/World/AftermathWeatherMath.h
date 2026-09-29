#pragma once

#include "CoreMinimal.h"

/**
 * Pure day/night and weather rules (unit-testable). The server advances one clock and one weather state;
 * clients receive both and derive the sun angle and visuals locally.
 */
namespace AftermathWeather
{
	struct FConfig
	{
		/** Real seconds per in-game day (default 1 game hour = 3 real minutes -> 72 minutes a day). */
		float DaySeconds = 72.f * 60.f;
		/** Hour the server starts at. */
		float StartHour = 9.f;
		float SunriseHour = 5.5f;
		float SunsetHour = 20.5f;
		/** Minimum / maximum real seconds between weather changes. */
		float WeatherChangeMin = 600.f;
		float WeatherChangeMax = 1800.f;
		/** How fast rain eases toward its target (per second). */
		float RainBlendPerSecond = 0.01f;
	};

	/** Hour of day [0, 24) after Elapsed real seconds. */
	AFTERMATH_API float HourAt(const FConfig& C, double ElapsedSeconds);

	/** Daylight 0 (deep night) .. 1 (full day), with a ~1 h twilight ramp at sunrise and sunset. */
	AFTERMATH_API float Daylight(const FConfig& C, float Hour);

	/** Sun pitch in degrees (negative = above the horizon, as a directional light pitch). */
	AFTERMATH_API float SunPitch(const FConfig& C, float Hour);

	/**
	 * Ambient warmth for the survival model (-1 very cold .. +1 very hot): colder at night, colder in rain.
	 * Mild spring-like climate: about +0.1 at midday, -0.5 before dawn.
	 */
	AFTERMATH_API float AmbientWarmth(float Daylight01, float Rain01);

	/** Pick the next rain target from a 0..1 random value: mostly dry, sometimes drizzle, rarely storms. */
	AFTERMATH_API float NextRainTarget(float Random01);

	/** Move Current toward Target by at most Rate * Dt. */
	AFTERMATH_API float BlendToward(float Current, float Target, float Rate, float Dt);
}
