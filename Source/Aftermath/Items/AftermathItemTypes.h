#pragma once

#include "CoreMinimal.h"
#include "AftermathItemTypes.generated.h"

/** Where a worn item goes. Head/Vest are armor; Hands holds the weapon you're using (DayZ-style "in hands"). */
UENUM(BlueprintType)
enum class EAftermathWearSlot : uint8
{
	None,
	Torso,
	Legs,
	Feet,
	Back,
	Head,
	Vest,
	Hands,
	Count UMETA(Hidden)
};

/** Hit zones for damage. */
UENUM(BlueprintType)
enum class EAftermathHitZone : uint8
{
	None,
	Head,
	Torso,
	Arms,
	Legs
};

/** What happens when you use an item on yourself. */
UENUM(BlueprintType)
enum class EAftermathUseAction : uint8
{
	None,
	Eat,
	Drink,
	Bandage,
	Splint,
	Medicine
};

/** DayZ-style condition bands, derived from health 0..1. */
UENUM(BlueprintType)
enum class EAftermathCondition : uint8
{
	Pristine,
	Worn,
	Damaged,
	BadlyDamaged,
	Ruined
};

USTRUCT(BlueprintType)
struct FAftermathGridSize
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 X = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 Y = 1;

	FAftermathGridSize() = default;
	FAftermathGridSize(int32 InX, int32 InY) : X(InX), Y(InY) {}

	bool IsValid() const { return X > 0 && Y > 0; }
	FAftermathGridSize Rotated() const { return FAftermathGridSize(Y, X); }
	bool operator==(const FAftermathGridSize& O) const { return X == O.X && Y == O.Y; }
};

USTRUCT(BlueprintType)
struct FAftermathUseDef
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	EAftermathUseAction Action = EAftermathUseAction::None;

	/** Per use (one "bite" / "sip"). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float Energy = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float Water = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	bool StopsBleeding = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	bool CuresSickness = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	bool CuresPoisoning = false;

	/** 0..1 chance to get food poisoning per use. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float PoisonChance = 0.f;

	/** 0..1 chance a dirty bandage gives you a cold (stand-in for wound infection). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float InfectionChance = 0.f;
};

USTRUCT(BlueprintType)
struct FAftermathWearDef
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	EAftermathWearSlot Slot = EAftermathWearSlot::None;

	/** 0..1 contribution to body warmth. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float Insulation = 0.f;

	/** Cargo grid while worn. 0x0 = no storage. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FAftermathGridSize Grid = FAftermathGridSize(0, 0);
};

USTRUCT(BlueprintType)
struct FAftermathMagazineDef
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName Ammo;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 Capacity = 0;
};

/** Firearm stats. Units: cm, cm/s, rounds per minute, degrees. */
USTRUCT(BlueprintType)
struct FAftermathFirearmDef
{
	GENERATED_BODY()

	/** Magazine item ids this gun accepts. Empty = not a firearm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TArray<FName> Magazines;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float Damage = 0.f;

	/** Blood loss (ml) on a hit, before zone and armor. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float BloodDamage = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float MuzzleVelocity = 35000.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float Rpm = 400.f;

	/** Beyond this distance the bullet is gone (server stops tracing). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float MaxRange = 50000.f;

	/** Cone half-angle while aiming / hip firing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float SpreadAimDeg = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float SpreadHipDeg = 2.5f;

	/** Camera kick per shot (client) - the server doesn't simulate recoil, it only checks aim plausibility. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float RecoilPitchDeg = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float RecoilYawDeg = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float ReloadSeconds = 2.f;

	/** How far the shot is heard (infected and HUD), cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float NoiseRadius = 20000.f;

	/** Holding the trigger keeps firing at Rpm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	bool bFullAuto = false;
};

/** Melee weapon stats. Fists are built-in (Damage from the survival tuning). */
USTRUCT(BlueprintType)
struct FAftermathMeleeDef
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float Damage = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float Range = 170.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float Cooldown = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float StaminaCost = 12.f;

	/** 0..1 chance to open a bleeding cut on a hit. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float CutChance = 0.f;
};

/** Protection when worn in Head or Vest. */
USTRUCT(BlueprintType)
struct FAftermathArmorDef
{
	GENERATED_BODY()

	/** Zone this protects. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	EAftermathHitZone Zone = EAftermathHitZone::None;

	/** 0..1 fraction of damage absorbed at full health (scales down as the armor degrades). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float Absorb = 0.f;

	/** Armor health lost per point of damage it absorbs (0.01 = 100 absorbed points ruins it). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float WearPerDamage = 0.005f;
};

/**
 * Static item definition, loaded from Content/Data/items.json. Field names match the JSON keys.
 * Runtime state (count, health, wetness, ...) lives in FAftermathItemInstance.
 */
USTRUCT(BlueprintType)
struct FAftermathItemDef
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName Id;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FString Name;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName Category;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FAftermathGridSize Size;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 WeightGrams = 0;

	/** >1 means the item stacks (bandages, ammo). Stackables never have Uses. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 MaxStack = 1;

	/** Portions for food/drink/medicine. 0 = not consumable by portions. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 Uses = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TArray<FName> Tags;

	/** Canned food: item this turns into once opened with a tool tagged OpensCans. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName OpensInto;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FAftermathUseDef Use;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FAftermathWearDef Wear;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FAftermathMagazineDef Magazine;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FAftermathFirearmDef Firearm;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FAftermathMeleeDef Melee;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FAftermathArmorDef Armor;

	bool HasTag(FName Tag) const { return Tags.Contains(Tag); }
	bool IsStackable() const { return MaxStack > 1; }
	bool IsWearable() const { return Wear.Slot != EAftermathWearSlot::None; }
	bool IsMagazine() const { return Magazine.Capacity > 0 && !Magazine.Ammo.IsNone(); }
	bool HasCargo() const { return IsWearable() && Wear.Grid.IsValid(); }
	bool IsFirearm() const { return Firearm.Magazines.Num() > 0 && Firearm.Damage > 0.f; }
	bool IsMeleeWeapon() const { return Melee.Damage > 0.f; }
	bool IsWeapon() const { return IsFirearm() || IsMeleeWeapon(); }
	bool IsArmor() const { return Armor.Zone != EAftermathHitZone::None && Armor.Absorb > 0.f; }
	bool AcceptsMagazine(FName MagId) const { return Firearm.Magazines.Contains(MagId); }
};

namespace AftermathItems
{
	/** Health 0..1 to condition band. Ruined at 0. */
	AFTERMATH_API EAftermathCondition ConditionFromHealth(float Health01);
	AFTERMATH_API const TCHAR* ConditionToString(EAftermathCondition Condition);
	AFTERMATH_API const TCHAR* WearSlotToString(EAftermathWearSlot Slot);

	/** Well-known tags. */
	AFTERMATH_API extern const FName TagOpensCans;
	AFTERMATH_API extern const FName TagCuts;
	AFTERMATH_API extern const FName TagTearable;
}
