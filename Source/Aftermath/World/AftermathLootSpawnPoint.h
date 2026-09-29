#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AftermathLootSpawnPoint.generated.h"

/**
 * A spot where the loot economy may place an item. Placed by the map builder inside buildings, tagged by
 * building type (House, Medical, Police, Military, Farm). Server-only data: never replicated, invisible.
 */
UCLASS()
class AFTERMATH_API AAftermathLootSpawnPoint : public AActor
{
	GENERATED_BODY()

public:
	AAftermathLootSpawnPoint();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	FName GetLootTag() const { return LootTag; }

	UPROPERTY(EditAnywhere, Category = "Loot")
	FName LootTag = TEXT("House");
};
