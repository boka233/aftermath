#include "Items/AftermathItemInstance.h"

#include "Items/AftermathItemDatabase.h"

namespace AftermathItems
{
	int32 NextUid()
	{
		// Game thread only (server logic runs there). Wraps long after any session ends; skip 0.
		static int32 Counter = 0;
		Counter = (Counter == MAX_int32) ? 1 : Counter + 1;
		return Counter;
	}

	FAftermathItemInstance MakeInstance(const FAftermathItemDef& Def, int32 Count)
	{
		FAftermathItemInstance Item;
		Item.Uid = NextUid();
		Item.Def = Def.Id;
		Item.Count = Def.IsStackable() ? FMath::Clamp(Count, 1, Def.MaxStack) : 1;
		Item.Uses = Def.Uses;
		Item.Health = 1.f;
		return Item;
	}

	int32 InstanceWeight(const FAftermathItemInstance& Item)
	{
		const UAftermathItemDatabase* DB = UAftermathItemDatabase::Get();
		const FAftermathItemDef* Def = DB ? DB->Find(Item.Def) : nullptr;
		if (!Def)
		{
			return 0;
		}

		int32 Grams = Def->WeightGrams * FMath::Max(1, Item.Count);
		if (Def->IsMagazine() && Item.Loaded > 0)
		{
			if (const FAftermathItemDef* Ammo = DB->Find(Def->Magazine.Ammo))
			{
				Grams += Ammo->WeightGrams * Item.Loaded;
			}
		}
		// A gun with a magazine in it carries the mag and its rounds.
		if (Item.HasMagazine())
		{
			if (const FAftermathItemDef* Mag = DB->Find(Item.AttachedMag))
			{
				Grams += Mag->WeightGrams;
				if (const FAftermathItemDef* Ammo = DB->Find(Mag->Magazine.Ammo))
				{
					Grams += Ammo->WeightGrams * Item.AttachedMagRounds;
				}
			}
		}
		// Consumables get lighter as they're used up (the container keeps ~20% of the weight).
		if (Def->Uses > 0 && Item.Uses < Def->Uses)
		{
			const float Left = static_cast<float>(FMath::Max(0, Item.Uses)) / Def->Uses;
			Grams = FMath::RoundToInt(Grams * (0.2f + 0.8f * Left));
		}
		return Grams;
	}
}
