#include "Items/AftermathItemDatabase.h"

#include "Aftermath.h"
#include "Engine/Engine.h"
#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

void UAftermathItemDatabase::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	const FString Path = GetDefaultPath();
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *Path))
	{
		UE_LOG(LogAftermath, Error, TEXT("[Aftermath] ItemDatabase: cannot read %s"), *Path);
		return;
	}

	TArray<FString> Errors;
	if (!LoadFromJsonString(Json, Errors))
	{
		for (const FString& E : Errors)
		{
			UE_LOG(LogAftermath, Error, TEXT("[Aftermath] ItemDatabase: %s"), *E);
		}
		return;
	}
	UE_LOG(LogAftermath, Display, TEXT("[Aftermath] ItemDatabase loaded items=%d from %s"), Defs.Num(), *Path);
}

UAftermathItemDatabase* UAftermathItemDatabase::Get()
{
	return GEngine ? GEngine->GetEngineSubsystem<UAftermathItemDatabase>() : nullptr;
}

FString UAftermathItemDatabase::GetDefaultPath()
{
	return FPaths::ProjectContentDir() / TEXT("Data/items.json");
}

const FAftermathItemDef* UAftermathItemDatabase::Find(FName Id) const
{
	return Defs.Find(Id);
}

bool UAftermathItemDatabase::LoadFromJsonString(const FString& Json, TArray<FString>& OutErrors)
{
	TArray<FAftermathItemDef> Parsed;
	FText FailReason;
	if (!FJsonObjectConverter::JsonArrayStringToUStruct(Json, &Parsed, 0, 0, false, &FailReason))
	{
		OutErrors.Add(FString::Printf(TEXT("JSON parse failed: %s"), *FailReason.ToString()));
		return false;
	}

	TMap<FName, FAftermathItemDef> NewDefs;
	TArray<FName> NewIds;
	for (const FAftermathItemDef& Def : Parsed)
	{
		if (Def.Id.IsNone())
		{
			OutErrors.Add(TEXT("item without Id"));
			continue;
		}
		if (NewDefs.Contains(Def.Id))
		{
			OutErrors.Add(FString::Printf(TEXT("%s: duplicate Id"), *Def.Id.ToString()));
			continue;
		}
		if (!Def.Size.IsValid() || Def.Size.X > 10 || Def.Size.Y > 10)
		{
			OutErrors.Add(FString::Printf(TEXT("%s: bad Size %dx%d"), *Def.Id.ToString(), Def.Size.X, Def.Size.Y));
		}
		if (Def.MaxStack < 1)
		{
			OutErrors.Add(FString::Printf(TEXT("%s: MaxStack must be >= 1"), *Def.Id.ToString()));
		}
		if (Def.MaxStack > 1 && Def.Uses > 0)
		{
			OutErrors.Add(FString::Printf(TEXT("%s: stackable items can't have Uses"), *Def.Id.ToString()));
		}
		if (Def.WeightGrams < 0)
		{
			OutErrors.Add(FString::Printf(TEXT("%s: negative weight"), *Def.Id.ToString()));
		}
		if ((Def.Use.Action == EAftermathUseAction::Eat || Def.Use.Action == EAftermathUseAction::Drink
			|| Def.Use.Action == EAftermathUseAction::Medicine) && Def.Uses <= 0)
		{
			OutErrors.Add(FString::Printf(TEXT("%s: consumable needs Uses > 0"), *Def.Id.ToString()));
		}
		if (Def.Wear.Grid.X < 0 || Def.Wear.Grid.Y < 0 || (Def.Wear.Grid.IsValid() && !Def.IsWearable()))
		{
			OutErrors.Add(FString::Printf(TEXT("%s: cargo Grid needs a wear Slot"), *Def.Id.ToString()));
		}
		NewDefs.Add(Def.Id, Def);
		NewIds.Add(Def.Id);
	}

	// Cross references.
	for (const TPair<FName, FAftermathItemDef>& Pair : NewDefs)
	{
		const FAftermathItemDef& Def = Pair.Value;
		if (Def.IsWeapon() && Def.Wear.Slot != EAftermathWearSlot::Hands)
		{
			OutErrors.Add(FString::Printf(TEXT("%s: weapons must use Wear.Slot Hands"), *Def.Id.ToString()));
		}
		if (Def.IsWeapon() && Def.IsStackable())
		{
			OutErrors.Add(FString::Printf(TEXT("%s: weapons can't stack"), *Def.Id.ToString()));
		}
		if (Def.IsFirearm() && (Def.Firearm.Rpm <= 0.f || Def.Firearm.MuzzleVelocity <= 0.f || Def.Firearm.MaxRange <= 0.f))
		{
			OutErrors.Add(FString::Printf(TEXT("%s: firearm needs Rpm, MuzzleVelocity and MaxRange > 0"), *Def.Id.ToString()));
		}
		for (const FName& MagId : Def.Firearm.Magazines)
		{
			const FAftermathItemDef* Mag = NewDefs.Find(MagId);
			if (!Mag || !Mag->IsMagazine())
			{
				OutErrors.Add(FString::Printf(TEXT("%s: accepts unknown magazine %s"), *Def.Id.ToString(), *MagId.ToString()));
			}
		}
		if (Def.IsArmor())
		{
			const bool bSlotMatches = (Def.Armor.Zone == EAftermathHitZone::Head && Def.Wear.Slot == EAftermathWearSlot::Head)
				|| (Def.Armor.Zone == EAftermathHitZone::Torso && Def.Wear.Slot == EAftermathWearSlot::Vest);
			if (!bSlotMatches || Def.Armor.Absorb > 0.95f)
			{
				OutErrors.Add(FString::Printf(TEXT("%s: armor zone/slot mismatch or Absorb > 0.95"), *Def.Id.ToString()));
			}
		}
		if (!Def.OpensInto.IsNone() && !NewDefs.Contains(Def.OpensInto))
		{
			OutErrors.Add(FString::Printf(TEXT("%s: OpensInto unknown item %s"), *Def.Id.ToString(), *Def.OpensInto.ToString()));
		}
		if (Def.Magazine.Capacity > 0)
		{
			const FAftermathItemDef* Ammo = NewDefs.Find(Def.Magazine.Ammo);
			if (!Ammo)
			{
				OutErrors.Add(FString::Printf(TEXT("%s: magazine ammo unknown %s"), *Def.Id.ToString(), *Def.Magazine.Ammo.ToString()));
			}
			else if (!Ammo->IsStackable())
			{
				OutErrors.Add(FString::Printf(TEXT("%s: magazine ammo %s must be stackable"), *Def.Id.ToString(), *Ammo->Id.ToString()));
			}
		}
	}

	if (OutErrors.Num() > 0)
	{
		return false;
	}

	Defs = MoveTemp(NewDefs);
	Ids = MoveTemp(NewIds);
	return true;
}
