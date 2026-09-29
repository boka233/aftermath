#include "World/AftermathLootSpawnPoint.h"

#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "World/AftermathLootManager.h"

AAftermathLootSpawnPoint::AAftermathLootSpawnPoint()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	// Server data only; clients never need these.
	bNetLoadOnClient = false;
}

void AAftermathLootSpawnPoint::BeginPlay()
{
	Super::BeginPlay();
	if (UAftermathLootManager* M = GetWorld() ? GetWorld()->GetSubsystem<UAftermathLootManager>() : nullptr)
	{
		M->RegisterPoint(this);
	}
}

void AAftermathLootSpawnPoint::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UAftermathLootManager* M = GetWorld() ? GetWorld()->GetSubsystem<UAftermathLootManager>() : nullptr)
	{
		M->UnregisterPoint(this);
	}
	Super::EndPlay(EndPlayReason);
}
