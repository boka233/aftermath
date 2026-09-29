#include "Core/AftermathGameState.h"

#include "Aftermath.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/SkyLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Infected/AftermathInfectedManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Net/UnrealNetwork.h"
#include "Player/AftermathCharacter.h"
#include "Survival/AftermathSurvivalComponent.h"

AAftermathGameState::AAftermathGameState()
{
	PrimaryActorTick.bCanEverTick = true;
	// The clock only needs a sync now and then; clients extrapolate in between.
	SetNetUpdateFrequency(1.f);
}

void AAftermathGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AAftermathGameState, ClockHourAtSync);
	DOREPLIFETIME(AAftermathGameState, ClockSyncServerTime);
	DOREPLIFETIME(AAftermathGameState, Rain);
}

void AAftermathGameState::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		// Server operators (and tests) can set the start hour: -AftermathHour=21.5
		float StartHour = Config.StartHour;
		FParse::Value(FCommandLine::Get(), TEXT("-AftermathHour="), StartHour);
		ClockHourAtSync = FMath::Fmod(FMath::Max(0.f, StartHour), 24.f);
		ClockSyncServerTime = GetServerWorldTimeSeconds();
		float StartRain = -1.f;
		if (FParse::Value(FCommandLine::Get(), TEXT("-AftermathRain="), StartRain))
		{
			ServerSetRain(StartRain, true);
		}
		NextWeatherChange = FMath::FRandRange(Config.WeatherChangeMin, Config.WeatherChangeMax);
		UE_LOG(LogAftermath, Display, TEXT("[Aftermath] WorldClock start_hour=%.2f day_seconds=%.0f rain=%.2f"), ClockHourAtSync, Config.DaySeconds, Rain);
	}
}

float AAftermathGameState::GetHour() const
{
	const double Elapsed = GetServerWorldTimeSeconds() - ClockSyncServerTime;
	AftermathWeather::FConfig C = Config;
	C.StartHour = ClockHourAtSync;
	return AftermathWeather::HourAt(C, FMath::Max(0.0, Elapsed));
}

void AAftermathGameState::ServerSetHour(float Hour)
{
	if (!HasAuthority())
	{
		return;
	}
	ClockHourAtSync = FMath::Fmod(FMath::Max(0.f, Hour), 24.f);
	ClockSyncServerTime = GetServerWorldTimeSeconds();
	ForceNetUpdate();
	ApplyToWorld();
}

void AAftermathGameState::ServerSetRain(float InRain, bool bHold)
{
	if (!HasAuthority())
	{
		return;
	}
	Rain = RainTarget = FMath::Clamp(InRain, 0.f, 1.f);
	bHoldRain = bHold;
	ForceNetUpdate();
	ApplyToWorld();
}

void AAftermathGameState::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (HasAuthority())
	{
		ServerTickWeather(DeltaSeconds);
	}
	if (GetNetMode() != NM_DedicatedServer)
	{
		ApplyVisuals();
	}
}

void AAftermathGameState::ServerTickWeather(float DeltaSeconds)
{
	if (!bHoldRain)
	{
		NextWeatherChange -= DeltaSeconds;
		if (NextWeatherChange <= 0.f)
		{
			RainTarget = AftermathWeather::NextRainTarget(FMath::FRand());
			NextWeatherChange = FMath::FRandRange(Config.WeatherChangeMin, Config.WeatherChangeMax);
			UE_LOG(LogAftermath, Display, TEXT("[Aftermath] WeatherChange rain_target=%.2f hour=%.2f"), RainTarget, GetHour());
		}
		const float NewRain = AftermathWeather::BlendToward(Rain, RainTarget, Config.RainBlendPerSecond, DeltaSeconds);
		// Only replicate noticeable changes.
		if (FMath::Abs(NewRain - Rain) > 0.f)
		{
			Rain = NewRain;
		}
	}

	// Push light and warmth to the systems that use them (twice a second is plenty).
	ApplyTimer -= DeltaSeconds;
	if (ApplyTimer <= 0.f)
	{
		ApplyTimer = 0.5f;
		ApplyToWorld();
	}
	LogTimer -= DeltaSeconds;
	if (LogTimer <= 0.f)
	{
		LogTimer = 60.f;
		UE_LOG(LogAftermath, Display, TEXT("[Aftermath] WorldClock hour=%.2f daylight=%.2f rain=%.2f warmth=%.2f"),
			GetHour(), GetDaylight(), Rain, AftermathWeather::AmbientWarmth(GetDaylight(), Rain));
	}
}

void AAftermathGameState::ApplyToWorld()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const float Light = GetDaylight();
	if (UAftermathInfectedManager* Infected = World->GetSubsystem<UAftermathInfectedManager>())
	{
		// Moonlight keeps a little vision at night.
		Infected->SetLight01(FMath::Max(Light, 0.1f));
		Infected->SetRain01(Rain);
	}
	FAftermathEnvironment Env;
	Env.AmbientWarmth = AftermathWeather::AmbientWarmth(Light, Rain);
	Env.Rain = Rain;
	for (TActorIterator<AAftermathCharacter> It(World); It; ++It)
	{
		if (UAftermathSurvivalComponent* S = It->GetSurvival())
		{
			S->SetEnvironment(Env);
		}
	}
}

void AAftermathGameState::ApplyVisuals()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const float Hour = GetHour();
	const float Light = GetDaylight();
	for (TActorIterator<ADirectionalLight> It(World); It; ++It)
	{
		UDirectionalLightComponent* L = Cast<UDirectionalLightComponent>(It->GetLightComponent());
		if (!L || L->Mobility != EComponentMobility::Movable)
		{
			continue;
		}
		const FRotator R(AftermathWeather::SunPitch(Config, Hour), 30.f + (Hour - 12.f) * 10.f, 0.f);
		It->SetActorRotation(R);
		// Overcast dims the sun; a faint moon stays at night.
		L->SetIntensity(FMath::Lerp(0.05f, 10.f, Light) * FMath::Lerp(1.f, 0.35f, Rain));
	}
	for (TActorIterator<AExponentialHeightFog> It(World); It; ++It)
	{
		if (UExponentialHeightFogComponent* Fog = It->GetComponent())
		{
			Fog->SetFogDensity(FMath::Lerp(0.02f, 0.08f, Rain));
		}
	}
}
