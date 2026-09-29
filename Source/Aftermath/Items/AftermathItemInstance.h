#pragma once

#include "CoreMinimal.h"
#include "Items/AftermathItemTypes.h"
#include "AftermathItemInstance.generated.h"

/**
 * One concrete item: a definition id plus mutable state. Plain value type so it can live in
 * containers, pickups, replication arrays and save files alike.
 */
USTRUCT(BlueprintType)
struct AFTERMATH_API FAftermathItemInstance
{
	GENERATED_BODY()

	/** Unique per server process; 0 = invalid. Lets clients refer to an item in RPCs. */
	UPROPERTY()
	int32 Uid = 0;

	UPROPERTY()
	FName Def;

	/** Stack count for stackables, 1 otherwise. */
	UPROPERTY()
	int32 Count = 1;

	/** Remaining portions for consumables (food, water, pills). */
	UPROPERTY()
	int32 Uses = 0;

	/** 0..1. Condition band derives from it. */
	UPROPERTY()
	float Health = 1.f;

	/** 0..1. Clothing gets wet in rain/water; wet clothes insulate badly. */
	UPROPERTY()
	float Wetness = 0.f;

	/** Magazines: rounds loaded. */
	UPROPERTY()
	int32 Loaded = 0;

	/** Firearms: attached magazine def (None = no mag) and its rounds. The mag's own state lives here while attached. */
	UPROPERTY()
	FName AttachedMag;

	UPROPERTY()
	int32 AttachedMagRounds = 0;

	/** Firearms: health of the attached magazine (so detaching gives it back as it was). */
	UPROPERTY()
	float AttachedMagHealth = 1.f;

	bool HasMagazine() const { return !AttachedMag.IsNone(); }

	bool IsValid() const { return Uid != 0 && !Def.IsNone(); }
	EAftermathCondition GetCondition() const { return AftermathItems::ConditionFromHealth(Health); }
};

namespace AftermathItems
{
	/** Fresh instance with defaults from its definition (full uses, full health). */
	AFTERMATH_API FAftermathItemInstance MakeInstance(const FAftermathItemDef& Def, int32 Count = 1);

	/** Server-only uid source. Never returns 0. */
	AFTERMATH_API int32 NextUid();

	/** Weight in grams of this instance including stack count and loaded rounds. */
	AFTERMATH_API int32 InstanceWeight(const FAftermathItemInstance& Item);
}
