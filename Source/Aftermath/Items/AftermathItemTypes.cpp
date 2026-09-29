#include "Items/AftermathItemTypes.h"

namespace AftermathItems
{
	const FName TagOpensCans(TEXT("OpensCans"));
	const FName TagCuts(TEXT("Cuts"));
	const FName TagTearable(TEXT("Tearable"));

	EAftermathCondition ConditionFromHealth(float Health01)
	{
		// DayZ bands: 100-70 pristine, 70-50 worn, 50-30 damaged, 30-0 badly damaged, 0 ruined.
		if (Health01 <= 0.f)   { return EAftermathCondition::Ruined; }
		if (Health01 < 0.30f)  { return EAftermathCondition::BadlyDamaged; }
		if (Health01 < 0.50f)  { return EAftermathCondition::Damaged; }
		if (Health01 < 0.70f)  { return EAftermathCondition::Worn; }
		return EAftermathCondition::Pristine;
	}

	const TCHAR* ConditionToString(EAftermathCondition Condition)
	{
		switch (Condition)
		{
		case EAftermathCondition::Pristine:     return TEXT("完好");
		case EAftermathCondition::Worn:         return TEXT("磨损");
		case EAftermathCondition::Damaged:      return TEXT("损坏");
		case EAftermathCondition::BadlyDamaged: return TEXT("严重损坏");
		case EAftermathCondition::Ruined:       return TEXT("毁坏");
		default:                                return TEXT("?");
		}
	}

	const TCHAR* WearSlotToString(EAftermathWearSlot Slot)
	{
		switch (Slot)
		{
		case EAftermathWearSlot::Torso: return TEXT("上衣");
		case EAftermathWearSlot::Legs:  return TEXT("裤子");
		case EAftermathWearSlot::Feet:  return TEXT("鞋");
		case EAftermathWearSlot::Back:  return TEXT("背包");
		case EAftermathWearSlot::Head:  return TEXT("头");
		case EAftermathWearSlot::Vest:  return TEXT("背心");
		case EAftermathWearSlot::Hands: return TEXT("手");
		default:                        return TEXT("-");
		}
	}
}
