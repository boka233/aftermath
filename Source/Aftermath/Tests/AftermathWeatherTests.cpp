// Stage 5 pure day/night and weather rules.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "World/AftermathWeatherMath.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAftermathWeatherTimeTest, "Aftermath.Weather.DayNight", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FAftermathWeatherTimeTest::RunTest(const FString& Parameters)
{
	using namespace AftermathWeather;
	FConfig C;

	TestEqual("start hour", HourAt(C, 0.0), C.StartHour);
	TestTrue("one game hour = 3 real minutes", FMath::IsNearlyEqual(HourAt(C, 180.0), C.StartHour + 1.f, 0.001f));
	TestTrue("wraps after a day", FMath::IsNearlyEqual(HourAt(C, C.DaySeconds), C.StartHour, 0.001f));
	const float H = HourAt(C, 1e7);
	TestTrue("always in [0,24)", H >= 0.f && H < 24.f);

	TestEqual("noon is day", Daylight(C, 12.f), 1.f);
	TestEqual("2am is night", Daylight(C, 2.f), 0.f);
	TestTrue("dawn is twilight", Daylight(C, C.SunriseHour) > 0.f && Daylight(C, C.SunriseHour) < 1.f);
	TestTrue("dusk is twilight", Daylight(C, C.SunsetHour) > 0.f && Daylight(C, C.SunsetHour) < 1.f);
	TestTrue("gets brighter through the morning", Daylight(C, 5.f) < Daylight(C, 6.f));

	TestTrue("sun above at noon", SunPitch(C, 13.f) < -50.f);
	TestTrue("sun below at midnight", SunPitch(C, 1.f) > 0.f);
	TestTrue("sun near the horizon at sunrise", FMath::Abs(SunPitch(C, C.SunriseHour)) < 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAftermathWeatherRainTest, "Aftermath.Weather.RainAndWarmth", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FAftermathWeatherRainTest::RunTest(const FString& Parameters)
{
	using namespace AftermathWeather;

	TestTrue("night colder than day", AmbientWarmth(0.f, 0.f) < AmbientWarmth(1.f, 0.f));
	TestTrue("rain makes it colder", AmbientWarmth(1.f, 1.f) < AmbientWarmth(1.f, 0.f));
	TestTrue("mild at noon", AmbientWarmth(1.f, 0.f) > 0.f);
	TestTrue("stormy night is cold", AmbientWarmth(0.f, 1.f) <= -0.7f);

	TestEqual("mostly dry", NextRainTarget(0.1f), 0.f);
	TestTrue("sometimes drizzle", NextRainTarget(0.7f) > 0.f && NextRainTarget(0.7f) < 1.f);
	TestEqual("rare storm", NextRainTarget(0.95f), 1.f);

	TestEqual("blends up", BlendToward(0.f, 1.f, 0.1f, 1.f), 0.1f);
	TestEqual("blends down", BlendToward(1.f, 0.f, 0.1f, 1.f), 0.9f);
	TestEqual("doesn't overshoot", BlendToward(0.95f, 1.f, 0.1f, 1.f), 1.f);
	return true;
}

#endif
