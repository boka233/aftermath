#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Infected/AftermathInfectedMath.h"
#include "AftermathInfectedManager.generated.h"

class AAftermathCharacter;
class AAftermathInfected;
class ANavigationData;
struct FAftermathNoiseEvent;

/**
 * Server-only brain scheduler for all infected in a world.
 *
 * Each tick it finds the nearest living player to every infected (one pass over a small player list),
 * asks AftermathInfectedMath::ThinkInterval how often that infected should think, and only runs the
 * brains that are due. Far, idle infected are put to sleep (actor tick off, movement off) and woken
 * when a player comes near or a noise reaches them. This keeps 150+ infected cheap: most frames only
 * the few near a player do any work.
 *
 * Also routes noises (UAftermathNoiseSubsystem::OnNoise) to infected in earshot and relays roars.
 */
UCLASS()
class AFTERMATH_API UAftermathInfectedManager : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	void Register(AAftermathInfected* Infected);
	void Unregister(AAftermathInfected* Infected);

	/** An infected with a target roars: others in RoarRadius that aren't busy come to look. */
	void Roar(AAftermathInfected* Source, AAftermathCharacter* Target);

	/** Spawn N infected on the navmesh around Center (server). Returns how many spawned. */
	int32 SpawnAround(const FVector& Center, float Radius, int32 Count, EAftermathInfectedType Type);

	/** 0 = dark, 1 = daylight (stage 5 day/night will drive this). */
	float GetLight01() const { return Light01; }
	void SetLight01(float InLight) { Light01 = FMath::Clamp(InLight, 0.f, 1.f); }

	float GetRain01() const { return Rain01; }

	int32 NumInfected() const { return Infected.Num(); }
	int32 NumAwake() const { return AwakeCount; }
	/** Brains run during the last tick (perf tests). */
	int32 LastThinks() const { return ThinksLastTick; }
	/** Average game-thread milliseconds spent in this manager's tick (moving average). */
	double GetAvgTickMs() const { return AvgTickMs; }

	/** Run every brain now regardless of distance (tests). */
	void ForceThinkAll();

	/** Automatic spawning from -AftermathInfected=N on the command line (server). */
	static int32 GetRequestedInfectedCount();

private:
	void HandleNoise(const FAftermathNoiseEvent& Event);
	void GatherPlayers();
	void SpawnFromCommandLine();

	UFUNCTION()
	void OnNavBuilt(ANavigationData* NavData);
	bool bLoggedNavBuilt = false;

	TArray<TWeakObjectPtr<AAftermathInfected>> Infected;
	TArray<AAftermathCharacter*> Players;
	FDelegateHandle NoiseHandle;
	float Light01 = 1.f;
	float Rain01 = 0.f;
	int32 AwakeCount = 0;
	int32 ThinksLastTick = 0;
	double AvgTickMs = 0.0;
	double StatsLogTime = 0.0;
	bool bSpawnedFromCommandLine = false;
};
