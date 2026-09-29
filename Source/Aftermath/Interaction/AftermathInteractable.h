#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "AftermathInteractable.generated.h"

class AAftermathCharacter;

UINTERFACE(MinimalAPI, NotBlueprintable)
class UAftermathInteractable : public UInterface
{
	GENERATED_BODY()
};

/**
 * Anything the player can use with the interact key (doors, pickups, containers later).
 * Interact() is only ever called on the server after range validation.
 */
class AFTERMATH_API IAftermathInteractable
{
	GENERATED_BODY()

public:
	/** Short prompt shown on the HUD, e.g. "开门". */
	virtual FText GetInteractPrompt(const AAftermathCharacter* User) const = 0;

	/** World point used for the server-side reach check. */
	virtual FVector GetInteractLocation() const = 0;

	/** Extra per-object rules (locked door, already taken...). Default: always allowed. */
	virtual bool CanInteract(const AAftermathCharacter* User) const { return true; }

	/** Server only. */
	virtual void Interact(AAftermathCharacter* User) = 0;
};
