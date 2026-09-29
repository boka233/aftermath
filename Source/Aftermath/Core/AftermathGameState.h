#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "World/AftermathWeatherMath.h"
#include "AftermathGameState.generated.h"

/**
 * Replicated world state everyone shares: time of day and weather.
 *
 * Server: advances the clock and the rain target, and pushes the resulting light level and warmth into
 * the infected manager and every survivor's environment. Clients: interpolate the clock locally between
 * updates (it replicates rarely) and drive the sun, sky light and fog from it.
 */
UCLASS()
class AFTERMATH_API AAftermathGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	AAftermathGameState();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Hour of day [0, 24) on this machine (clients extrapolate between updates). */
	float GetHour() const;
	float GetDaylight() const { return AftermathWeather::Daylight(Config, GetHour()); }
	float GetRain() const { return Rain; }

	/** Server/dev: jump to an hour, force rain (tests, admin). */
	void ServerSetHour(float Hour);
	void ServerSetRain(float InRain, bool bHold = true);

	const AftermathWeather::FConfig& GetWeatherConfig() const { return Config; }

protected:
	/** Server time (world seconds) at which the clock read ClockHourAtSync. */
	UPROPERTY(Replicated)
	float ClockHourAtSync = 9.f;

	UPROPERTY(Replicated)
	double ClockSyncServerTime = 0.0;

	UPROPERTY(Replicated)
	float Rain = 0.f;

private:
	void ServerTickWeather(float DeltaSeconds);
	void ApplyToWorld();
	void ApplyVisuals();

	AftermathWeather::FConfig Config;
	float RainTarget = 0.f;
	float NextWeatherChange = 0.f;
	bool bHoldRain = false;
	float ApplyTimer = 0.f;
	float LogTimer = 0.f;
};
