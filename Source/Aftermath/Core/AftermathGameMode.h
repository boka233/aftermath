#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "AftermathGameMode.generated.h"

/**
 * Server-only game rules. Exists only on the server (never replicated to clients).
 * Spawns fresh survivors with a starting loadout and respawns the dead after a delay.
 */
UCLASS()
class AFTERMATH_API AAftermathGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AAftermathGameMode();

	virtual void PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage) override;
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;
	virtual void RestartPlayer(AController* NewPlayer) override;
	virtual APawn* SpawnDefaultPawnAtTransform_Implementation(AController* NewPlayer, const FTransform& SpawnTransform) override;

	/** Called by a survivor's death: schedule a fresh spawn. The corpse stays in the world. */
	void OnSurvivorDied(class AAftermathCharacter* Victim);

protected:
	/** Hard cap on concurrent players. Overridable in DefaultGame.ini / server ini. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Server")
	int32 MaxPlayers = 64;

	UPROPERTY(Config, EditDefaultsOnly, Category = "Server")
	float RespawnDelay = 8.f;

	/** How long corpses (and their dropped gear) stay before cleanup. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Server")
	float CorpseLifetime = 600.f;
};
