#include "Core/AftermathGameMode.h"

#include "Aftermath.h"
#include "Items/AftermathInventoryComponent.h"
#include "Player/AftermathCharacter.h"
#include "Player/AftermathPlayerController.h"
#include "UI/AftermathHUD.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "TimerManager.h"

AAftermathGameMode::AAftermathGameMode()
{
	DefaultPawnClass = AAftermathCharacter::StaticClass();
	PlayerControllerClass = AAftermathPlayerController::StaticClass();
	HUDClass = AAftermathHUD::StaticClass();
	// Spawn point selection: a client URL portal ("127.0.0.1:7777#StartA") picks the PlayerStart
	// whose PlayerStartTag matches (engine FindPlayerStart behaviour). Used by automated tests.
}

void AAftermathGameMode::PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage)
{
	Super::PreLogin(Options, Address, UniqueId, ErrorMessage);

	if (ErrorMessage.IsEmpty() && GetNumPlayers() >= MaxPlayers)
	{
		ErrorMessage = TEXT("Server full");
	}
}

void AAftermathGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);

	const APlayerState* PS = NewPlayer ? NewPlayer->GetPlayerState<APlayerState>() : nullptr;
	// Stable marker line: automated connection tests grep the server log for it.
	UE_LOG(LogAftermath, Display, TEXT("[Aftermath] PlayerJoined name=%s players=%d"),
		PS ? *PS->GetPlayerName() : TEXT("?"), GetNumPlayers());
}

void AAftermathGameMode::Logout(AController* Exiting)
{
	const APlayerState* PS = Exiting ? Exiting->GetPlayerState<APlayerState>() : nullptr;
	UE_LOG(LogAftermath, Display, TEXT("[Aftermath] PlayerLeft name=%s"), PS ? *PS->GetPlayerName() : TEXT("?"));

	Super::Logout(Exiting);
}

APawn* AAftermathGameMode::SpawnDefaultPawnAtTransform_Implementation(AController* NewPlayer, const FTransform& SpawnTransform)
{
	// The engine default refuses to spawn when something stands on the spawn point, which leaves the
	// player with no body at all. Infected (or another player) camping a spawn must never do that:
	// nudge the capsule to the nearest free spot instead, and only as a last resort spawn anyway.
	FActorSpawnParameters SpawnInfo;
	SpawnInfo.Instigator = GetInstigator();
	SpawnInfo.ObjectFlags |= RF_Transient;
	SpawnInfo.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	UClass* PawnClass = GetDefaultPawnClassForController(NewPlayer);
	APawn* Pawn = GetWorld()->SpawnActor<APawn>(PawnClass, SpawnTransform, SpawnInfo);
	if (!Pawn)
	{
		UE_LOG(LogAftermath, Error, TEXT("[Aftermath] SpawnFailed at=%s"), *SpawnTransform.GetLocation().ToCompactString());
	}
	return Pawn;
}

void AAftermathGameMode::RestartPlayer(AController* NewPlayer)
{
	Super::RestartPlayer(NewPlayer);

	AAftermathCharacter* Survivor = NewPlayer ? Cast<AAftermathCharacter>(NewPlayer->GetPawn()) : nullptr;
	if (!Survivor)
	{
		return;
	}
	if (UAftermathInventoryComponent* Inv = Survivor->GetInventory())
	{
		Inv->GiveStartingLoadout();
	}
	UE_LOG(LogAftermath, Display, TEXT("[Aftermath] PlayerSpawned pawn=%s at=%s"), *Survivor->GetName(), *Survivor->GetActorLocation().ToCompactString());
}

void AAftermathGameMode::OnSurvivorDied(AAftermathCharacter* Victim)
{
	if (!Victim)
	{
		return;
	}
	AController* Controller = Victim->GetController();
	Victim->SetLifeSpan(CorpseLifetime);
	if (!Controller)
	{
		return;
	}

	// Detach the player from the corpse so the body stays where it fell.
	Controller->UnPossess();

	TWeakObjectPtr<AController> WeakController = Controller;
	FTimerHandle Handle;
	GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this, [this, WeakController]()
	{
		if (AController* C = WeakController.Get())
		{
			RestartPlayer(C);
		}
	}), RespawnDelay, false);
}
