// Stage 3 weapon/armor data and magazine handling (server rules on a real character's inventory).

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#include "Engine/World.h"
#include "Items/AftermathInventoryComponent.h"
#include "Items/AftermathItemActions.h"
#include "Items/AftermathItemDatabase.h"
#include "Player/AftermathCharacter.h"

namespace AftermathWeaponItemTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	const FAftermathItemDef* Def(FName Id)
	{
		return UAftermathItemDatabase::Get()->Find(Id);
	}

	FAftermathItemInstance Make(FName Id, int32 Count = 1)
	{
		const FAftermathItemDef* D = Def(Id);
		return D ? AftermathItems::MakeInstance(*D, Count) : FAftermathItemInstance();
	}

	struct FWorld
	{
		FTestWorldWrapper Wrapper;
		AAftermathCharacter* C = nullptr;
		UAftermathInventoryComponent* Inv = nullptr;

		bool Init()
		{
			if (!Wrapper.CreateTestWorld(EWorldType::Game) || !Wrapper.BeginPlayInTestWorld())
			{
				return false;
			}
			FActorSpawnParameters P;
			P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			C = Wrapper.GetTestWorld()->SpawnActor<AAftermathCharacter>(FVector(0, 0, 100.f), FRotator::ZeroRotator, P);
			Inv = C ? C->GetInventory() : nullptr;
			return Inv != nullptr;
		}

		~FWorld()
		{
			Wrapper.EndPlayInTestWorld();
			Wrapper.DestroyTestWorld(true);
		}
	};
}

using namespace AftermathWeaponItemTest;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAftermathWeaponDataTest, "Aftermath.Weapons.Data", AftermathWeaponItemTest::Flags)
bool FAftermathWeaponDataTest::RunTest(const FString& Parameters)
{
	const FAftermathItemDef* Pistol = Def(TEXT("Pistol"));
	const FAftermathItemDef* Rifle = Def(TEXT("Rifle"));
	const FAftermathItemDef* Axe = Def(TEXT("Axe"));
	const FAftermathItemDef* Helmet = Def(TEXT("Helmet"));
	const FAftermathItemDef* Vest = Def(TEXT("PlateCarrier"));
	UTEST_TRUE("all defined", Pistol && Rifle && Axe && Helmet && Vest);

	UTEST_TRUE("pistol is a firearm", Pistol->IsFirearm() && !Pistol->IsMeleeWeapon());
	UTEST_TRUE("pistol takes 9mm mags", Pistol->AcceptsMagazine(TEXT("Mag9mm")) && !Pistol->AcceptsMagazine(TEXT("Mag545")));
	UTEST_TRUE("rifle full auto", Rifle->Firearm.bFullAuto);
	UTEST_TRUE("rifle outranges pistol", Rifle->Firearm.MaxRange > Pistol->Firearm.MaxRange);
	UTEST_TRUE("rifle is more accurate when aimed", Rifle->Firearm.SpreadAimDeg < Pistol->Firearm.SpreadAimDeg);
	UTEST_EQUAL("weapons go in hands", Pistol->Wear.Slot, EAftermathWearSlot::Hands);
	UTEST_TRUE("axe is melee and cuts", Axe->IsMeleeWeapon() && Axe->HasTag(AftermathItems::TagCuts));
	UTEST_TRUE("helmet protects head", Helmet->IsArmor() && Helmet->Armor.Zone == EAftermathHitZone::Head && Helmet->Wear.Slot == EAftermathWearSlot::Head);
	UTEST_TRUE("vest protects torso and has pouches", Vest->IsArmor() && Vest->Armor.Zone == EAftermathHitZone::Torso && Vest->HasCargo());

	// Validation catches broken weapon data.
	UAftermathItemDatabase* Scratch = NewObject<UAftermathItemDatabase>(GetTransientPackage());
	TArray<FString> Errors;
	UTEST_TRUE("gun outside hands rejected", !Scratch->LoadFromJsonString(
		TEXT(R"([{"Id":"M","Size":{"X":1,"Y":1},"Magazine":{"Ammo":"A","Capacity":5}},{"Id":"A","Size":{"X":1,"Y":1},"MaxStack":10},)"
		     R"({"Id":"G","Size":{"X":2,"Y":2},"Wear":{"Slot":"Back"},"Firearm":{"Magazines":["M"],"Damage":10}}])"), Errors));
	UTEST_TRUE("mentions hands", Errors.ContainsByPredicate([](const FString& E) { return E.Contains(TEXT("Hands")); }));
	Errors.Reset();
	UTEST_TRUE("unknown mag rejected", !Scratch->LoadFromJsonString(
		TEXT(R"([{"Id":"G","Size":{"X":2,"Y":2},"Wear":{"Slot":"Hands"},"Firearm":{"Magazines":["Nope"],"Damage":10}}])"), Errors));
	Errors.Reset();
	UTEST_TRUE("helmet on vest slot rejected", !Scratch->LoadFromJsonString(
		TEXT(R"([{"Id":"H","Size":{"X":2,"Y":2},"Wear":{"Slot":"Vest"},"Armor":{"Zone":"Head","Absorb":0.5}}])"), Errors));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAftermathWeaponMagTest, "Aftermath.Weapons.MagazinesAttachReload", AftermathWeaponItemTest::Flags)
bool FAftermathWeaponMagTest::RunTest(const FString& Parameters)
{
	FWorld W;
	UTEST_TRUE("world", W.Init());
	UAftermathInventoryComponent* Inv = W.Inv;
	AAftermathCharacter& C = *W.C;
	Inv->AddItem(Make(TEXT("Backpack")));

	// Weapons are "worn" in the hands slot.
	FAftermathItemInstance Pistol = Make(TEXT("Pistol"));
	UTEST_EQUAL("pistol taken in hands", Inv->AddItem(Pistol), 0);
	UTEST_EQUAL("hands slot holds it", Inv->GetSlot(EAftermathWearSlot::Hands)->Item.Uid, Pistol.Uid);
	UTEST_NULL("hands have no cargo grid", Inv->GetGrid(EAftermathWearSlot::Hands));

	// A second weapon goes to the backpack (hands are full).
	FAftermathItemInstance Rifle = Make(TEXT("Rifle"));
	UTEST_EQUAL("rifle stored", Inv->AddItem(Rifle), 0);
	FAftermathItemLocation Loc;
	Inv->FindItem(Rifle.Uid, &Loc);
	UTEST_TRUE("rifle in backpack", !Loc.bWorn && Loc.Slot == EAftermathWearSlot::Back);

	// Two 9mm mags, one full and one with 5 rounds.
	FAftermathItemInstance MagA = Make(TEXT("Mag9mm"));
	MagA.Loaded = 15;
	FAftermathItemInstance MagB = Make(TEXT("Mag9mm"));
	MagB.Loaded = 5;
	Inv->AddItem(MagA);
	Inv->AddItem(MagB);
	FAftermathItemInstance RifleMag = Make(TEXT("Mag545"));
	RifleMag.Loaded = 30;
	Inv->AddItem(RifleMag);

	// Wrong magazine type is refused.
	UTEST_EQUAL("rifle mag doesn't fit pistol", AftermathItemActions::AttachMagazine(C, Pistol.Uid, RifleMag.Uid), EAftermathActionResult::NotUsable);

	// Drag the half-empty mag onto the pistol.
	const int32 WeightNoMag = AftermathItems::InstanceWeight(*Inv->FindItem(Pistol.Uid));
	UTEST_EQUAL("attach mag B", AftermathItemActions::Combine(C, MagB.Uid, Pistol.Uid), EAftermathActionResult::Ok);
	const FAftermathItemInstance* Gun = Inv->FindItem(Pistol.Uid);
	UTEST_TRUE("mag in gun", Gun->AttachedMag == TEXT("Mag9mm") && Gun->AttachedMagRounds == 5);
	UTEST_NULL("mag B left the backpack", Inv->FindItem(MagB.Uid));
	UTEST_TRUE("gun heavier with a loaded mag", AftermathItems::InstanceWeight(*Gun) > WeightNoMag);

	// Reload picks the fuller mag and puts the old one back.
	UTEST_EQUAL("reload", AftermathItemActions::Reload(C, Pistol.Uid), EAftermathActionResult::Ok);
	Gun = Inv->FindItem(Pistol.Uid);
	UTEST_EQUAL("full mag in", Gun->AttachedMagRounds, 15);
	UTEST_NULL("full mag left the backpack", Inv->FindItem(MagA.Uid));
	int32 LooseMags = 0, LooseRounds = 0;
	for (const FAftermathGridEntry& E : Inv->GetGrid(EAftermathWearSlot::Back)->Entries)
	{
		if (E.Item.Def == TEXT("Mag9mm")) { ++LooseMags; LooseRounds += E.Item.Loaded; }
	}
	UTEST_TRUE("old mag back with its 5 rounds", LooseMags == 1 && LooseRounds == 5);

	// Nothing fuller to swap to.
	UTEST_EQUAL("no better mag", AftermathItemActions::Reload(C, Pistol.Uid), EAftermathActionResult::NeedsTool);

	// Detach.
	UTEST_EQUAL("detach", AftermathItemActions::DetachMagazine(C, Pistol.Uid), EAftermathActionResult::Ok);
	UTEST_TRUE("gun empty", !Inv->FindItem(Pistol.Uid)->HasMagazine());
	UTEST_EQUAL("two 9mm mags carried", Inv->CountOf(TEXT("Mag9mm")), 2);
	UTEST_EQUAL("detach empty gun", AftermathItemActions::DetachMagazine(C, Pistol.Uid), EAftermathActionResult::NothingToTreat);

	// Rifle in the backpack: "use" takes it in hands only when hands are free.
	UTEST_EQUAL("hands busy", AftermathItemActions::Use(C, Rifle.Uid), EAftermathActionResult::NoRoom);
	UTEST_TRUE("put pistol away", Inv->Unequip(EAftermathWearSlot::Hands, EAftermathWearSlot::Back, 0, 4, false));
	UTEST_EQUAL("take rifle in hands", AftermathItemActions::Use(C, Rifle.Uid), EAftermathActionResult::Ok);
	UTEST_EQUAL("rifle in hands", Inv->GetSlot(EAftermathWearSlot::Hands)->Item.Uid, Rifle.Uid);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAftermathArmorWearTest, "Aftermath.Weapons.ArmorSlots", AftermathWeaponItemTest::Flags)
bool FAftermathArmorWearTest::RunTest(const FString& Parameters)
{
	FWorld W;
	UTEST_TRUE("world", W.Init());
	UAftermathInventoryComponent* Inv = W.Inv;
	UTEST_EQUAL("helmet worn", Inv->AddItem(Make(TEXT("Helmet"))), 0);
	UTEST_EQUAL("vest worn", Inv->AddItem(Make(TEXT("PlateCarrier"))), 0);
	UTEST_TRUE("head slot", !Inv->GetSlot(EAftermathWearSlot::Head)->IsEmpty());
	const FAftermathGrid* Pouches = Inv->GetGrid(EAftermathWearSlot::Vest);
	UTEST_TRUE("vest pouches 2x2", Pouches && Pouches->Width == 2 && Pouches->Height == 2);
	UTEST_EQUAL("bandage goes into the vest", Inv->AddItem(Make(TEXT("Bandage"))), 0);
	UTEST_EQUAL("in vest", Pouches->CountOf(TEXT("Bandage")), 1);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
