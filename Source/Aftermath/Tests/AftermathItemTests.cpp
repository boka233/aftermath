// Stage 2 pure-logic tests: item database validation and grid container rules.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Items/AftermathGrid.h"
#include "Items/AftermathItemDatabase.h"
#include "Items/AftermathItemInstance.h"

namespace AftermathItemTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	const FAftermathItemDef* Def(FName Id)
	{
		const UAftermathItemDatabase* DB = UAftermathItemDatabase::Get();
		return DB ? DB->Find(Id) : nullptr;
	}

	FAftermathItemInstance Make(FName Id, int32 Count = 1)
	{
		const FAftermathItemDef* D = Def(Id);
		return D ? AftermathItems::MakeInstance(*D, Count) : FAftermathItemInstance();
	}
}

using namespace AftermathItemTest;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAftermathItemDbShippedTest, "Aftermath.Items.DatabaseLoadsShippedFile", AftermathItemTest::Flags)
bool FAftermathItemDbShippedTest::RunTest(const FString& Parameters)
{
	const UAftermathItemDatabase* DB = UAftermathItemDatabase::Get();
	UTEST_NOT_NULL("database subsystem", DB);
	UTEST_TRUE("loaded items.json", DB->Num() >= 15);

	const FAftermathItemDef* Bandage = Def(TEXT("Bandage"));
	UTEST_NOT_NULL("Bandage", Bandage);
	UTEST_EQUAL("Bandage name", Bandage->Name, FString(TEXT("绷带")));
	UTEST_TRUE("Bandage stacks", Bandage->IsStackable());
	UTEST_TRUE("Bandage stops bleeding", Bandage->Use.StopsBleeding);

	const FAftermathItemDef* Backpack = Def(TEXT("Backpack"));
	UTEST_NOT_NULL("Backpack", Backpack);
	UTEST_EQUAL("Backpack slot", Backpack->Wear.Slot, EAftermathWearSlot::Back);
	UTEST_TRUE("Backpack cargo", Backpack->HasCargo() && Backpack->Wear.Grid.X == 6 && Backpack->Wear.Grid.Y == 6);

	const FAftermathItemDef* Can = Def(TEXT("CannedBeans"));
	UTEST_NOT_NULL("CannedBeans", Can);
	UTEST_EQUAL("can opens into", Can->OpensInto, FName(TEXT("CannedBeansOpen")));

	const FAftermathItemDef* Mag = Def(TEXT("Mag9mm"));
	UTEST_TRUE("magazine", Mag && Mag->IsMagazine() && Mag->Magazine.Capacity == 15);

	UTEST_TRUE("Knife opens cans", Def(TEXT("Knife")) && Def(TEXT("Knife"))->HasTag(AftermathItems::TagOpensCans));
	UTEST_NULL("unknown id", Def(TEXT("NoSuchItem")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAftermathItemDbValidationTest, "Aftermath.Items.DatabaseRejectsBadData", AftermathItemTest::Flags)
bool FAftermathItemDbValidationTest::RunTest(const FString& Parameters)
{
	// Scratch database object so the shared one is never touched.
	UAftermathItemDatabase* DB = NewObject<UAftermathItemDatabase>(GetTransientPackage());

	struct FCase { const TCHAR* Json; const TCHAR* Expect; };
	const FCase Bad[] = {
		{ TEXT("not json"), TEXT("parse") },
		{ TEXT(R"([{"Id":"A","Size":{"X":1,"Y":1}},{"Id":"A","Size":{"X":1,"Y":1}}])"), TEXT("duplicate") },
		{ TEXT(R"([{"Id":"A","Size":{"X":0,"Y":1}}])"), TEXT("bad Size") },
		{ TEXT(R"([{"Id":"A","Size":{"X":1,"Y":1},"MaxStack":5,"Uses":3}])"), TEXT("stackable") },
		{ TEXT(R"([{"Id":"A","Size":{"X":1,"Y":1},"OpensInto":"B"}])"), TEXT("OpensInto") },
		{ TEXT(R"([{"Id":"M","Size":{"X":1,"Y":1},"Magazine":{"Ammo":"X","Capacity":10}}])"), TEXT("magazine ammo") },
		{ TEXT(R"([{"Id":"F","Size":{"X":1,"Y":1},"Use":{"Action":"Eat","Energy":5}}])"), TEXT("Uses") },
		{ TEXT(R"([{"Id":"G","Size":{"X":1,"Y":1},"Wear":{"Grid":{"X":2,"Y":2}}}])"), TEXT("wear Slot") },
	};
	for (const FCase& C : Bad)
	{
		TArray<FString> Errors;
		const bool bOk = DB->LoadFromJsonString(C.Json, Errors);
		const bool bMentions = Errors.ContainsByPredicate([&C](const FString& E) { return E.Contains(C.Expect); });
		if (bOk || !bMentions)
		{
			AddError(FString::Printf(TEXT("expected failure mentioning '%s' for %s; got ok=%d errors=[%s]"),
				C.Expect, C.Json, bOk, *FString::Join(Errors, TEXT("; "))));
		}
	}
	UTEST_EQUAL("failed loads keep the database empty", DB->Num(), 0);

	TArray<FString> Errors;
	UTEST_TRUE("valid data loads", DB->LoadFromJsonString(TEXT(R"([{"Id":"Ok","Size":{"X":2,"Y":1},"WeightGrams":10}])"), Errors));
	UTEST_EQUAL("one item", DB->Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAftermathGridPlacementTest, "Aftermath.Items.GridPlacement", AftermathItemTest::Flags)
bool FAftermathGridPlacementTest::RunTest(const FString& Parameters)
{
	FAftermathGrid G(4, 3);
	const FAftermathItemInstance Bottle = Make(TEXT("WaterBottle")); // 1x2
	const FAftermathItemInstance Splint = Make(TEXT("Splint"));      // 1x3
	UTEST_TRUE("instances valid", Bottle.IsValid() && Splint.IsValid());

	UTEST_TRUE("place bottle at 0,0", G.Place(Bottle, 0, 0, false));
	UTEST_EQUAL("cell 0,0", G.UidAt(0, 0), Bottle.Uid);
	UTEST_EQUAL("cell 0,1", G.UidAt(0, 1), Bottle.Uid);
	UTEST_EQUAL("cell 1,0 empty", G.UidAt(1, 0), 0);

	UTEST_TRUE("overlap rejected", !G.CanPlace(Splint, 0, 1, false));
	UTEST_TRUE("out of bounds rejected", !G.CanPlace(Splint, 3, 1, false));
	UTEST_TRUE("negative rejected", !G.CanPlace(Splint, -1, 0, false));
	UTEST_TRUE("rotated splint (3x1) fits on the top row", G.CanPlace(Splint, 1, 0, true));
	UTEST_TRUE("rotated splint too wide at x=2", !G.CanPlace(Splint, 2, 0, true));
	UTEST_TRUE("same uid twice rejected", !G.Place(Bottle, 2, 0, false));

	UTEST_TRUE("move bottle", G.MoveWithin(Bottle.Uid, 3, 1, false));
	UTEST_EQUAL("old cell freed", G.UidAt(0, 0), 0);
	UTEST_TRUE("move rotated into place", G.MoveWithin(Bottle.Uid, 2, 2, true));
	UTEST_TRUE("move out of bounds refused", !G.MoveWithin(Bottle.Uid, 3, 2, true));
	UTEST_EQUAL("still at last good spot", G.FindEntry(Bottle.Uid)->X, 2);

	FAftermathItemInstance Out;
	UTEST_TRUE("remove", G.Remove(Bottle.Uid, Out));
	UTEST_EQUAL("removed item", Out.Uid, Bottle.Uid);
	UTEST_TRUE("remove twice fails", !G.Remove(Bottle.Uid, Out));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAftermathGridFreeSpotTest, "Aftermath.Items.GridFreeSpotUsesRotation", AftermathItemTest::Flags)
bool FAftermathGridFreeSpotTest::RunTest(const FString& Parameters)
{
	// 3 wide x 1 tall: a 1x3 splint only fits rotated.
	FAftermathGrid G(3, 1);
	const FAftermathItemInstance Splint = Make(TEXT("Splint"));
	int32 X = -1, Y = -1;
	bool bRot = false;
	UTEST_TRUE("found spot", G.FindFreeSpot(Splint, X, Y, bRot));
	UTEST_TRUE("had to rotate", bRot && X == 0 && Y == 0);
	UTEST_EQUAL("add anywhere stores it", G.AddAnywhere(Splint), 0);
	UTEST_EQUAL("grid full", G.AddAnywhere(Make(TEXT("Bandage"))), 1);

	FAftermathGrid Tiny(1, 1);
	UTEST_EQUAL("too big for tiny grid", Tiny.AddAnywhere(Make(TEXT("WaterBottle"))), 1);
	UTEST_TRUE("tiny grid unchanged", Tiny.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAftermathGridStackingTest, "Aftermath.Items.GridStackSplitMerge", AftermathItemTest::Flags)
bool FAftermathGridStackingTest::RunTest(const FString& Parameters)
{
	const FAftermathItemDef* Ammo = Def(TEXT("Ammo9mm"));
	UTEST_NOT_NULL("ammo def", Ammo);
	const int32 Max = Ammo->MaxStack;

	FAftermathGrid G(2, 1);
	FAftermathItemInstance A = Make(TEXT("Ammo9mm"), Max - 10);
	UTEST_EQUAL("first stack stored", G.AddAnywhere(A), 0);

	// 30 more: 10 top up the first stack, 20 go to a new stack.
	UTEST_EQUAL("second add stored", G.AddAnywhere(Make(TEXT("Ammo9mm"), 30)), 0);
	UTEST_EQUAL("two stacks", G.Entries.Num(), 2);
	UTEST_EQUAL("first stack full", G.FindEntry(A.Uid)->Item.Count, Max);
	UTEST_EQUAL("total", G.CountOf(Ammo->Id), Max + 20);

	// Grid is full (2 cells, 2 stacks): only topping up is possible.
	UTEST_EQUAL("overflow refused", G.AddAnywhere(Make(TEXT("Ammo9mm"), Max)), 20);
	UTEST_EQUAL("second stack topped up", G.CountOf(Ammo->Id), Max * 2);

	// Split needs a free cell.
	const int32 SecondUid = G.Entries[1].Item.Uid;
	UTEST_TRUE("split fails without room", !G.Split(SecondUid, 5, AftermathItems::NextUid()));

	FAftermathGrid H(3, 1);
	FAftermathItemInstance S = Make(TEXT("Ammo9mm"), 40);
	H.AddAnywhere(S);
	const int32 NewUid = AftermathItems::NextUid();
	UTEST_TRUE("split 15", H.Split(S.Uid, 15, NewUid));
	UTEST_EQUAL("source left", H.FindEntry(S.Uid)->Item.Count, 25);
	UTEST_EQUAL("new stack", H.FindEntry(NewUid)->Item.Count, 15);
	UTEST_TRUE("split all refused", !H.Split(S.Uid, 25, AftermathItems::NextUid()));
	UTEST_TRUE("split zero refused", !H.Split(S.Uid, 0, AftermathItems::NextUid()));

	UTEST_EQUAL("merge back", H.MergeInto(NewUid, S.Uid), 15);
	UTEST_EQUAL("merged count", H.FindEntry(S.Uid)->Item.Count, 40);
	UTEST_NULL("emptied source removed", H.FindEntry(NewUid));
	UTEST_EQUAL("merge with self", H.MergeInto(S.Uid, S.Uid), 0);

	// Non-stackables never merge.
	FAftermathGrid K(3, 3);
	FAftermathItemInstance K1 = Make(TEXT("Knife"));
	FAftermathItemInstance K2 = Make(TEXT("Knife"));
	K.AddAnywhere(K1);
	K.AddAnywhere(K2);
	UTEST_EQUAL("knives are separate", K.Entries.Num(), 2);
	UTEST_EQUAL("no knife merge", K.MergeInto(K2.Uid, K1.Uid), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAftermathItemWeightTest, "Aftermath.Items.WeightAndCondition", AftermathItemTest::Flags)
bool FAftermathItemWeightTest::RunTest(const FString& Parameters)
{
	FAftermathItemInstance Ammo = Make(TEXT("Ammo9mm"), 20);
	UTEST_EQUAL("stack weight", AftermathItems::InstanceWeight(Ammo), Def(TEXT("Ammo9mm"))->WeightGrams * 20);

	FAftermathItemInstance Mag = Make(TEXT("Mag9mm"));
	Mag.Loaded = 10;
	UTEST_EQUAL("loaded magazine weight", AftermathItems::InstanceWeight(Mag),
		Def(TEXT("Mag9mm"))->WeightGrams + Def(TEXT("Ammo9mm"))->WeightGrams * 10);

	FAftermathItemInstance Bottle = Make(TEXT("WaterBottle"));
	const int32 Full = AftermathItems::InstanceWeight(Bottle);
	Bottle.Uses = 0;
	UTEST_TRUE("empty bottle is lighter", AftermathItems::InstanceWeight(Bottle) < Full / 2);

	using AftermathItems::ConditionFromHealth;
	UTEST_EQUAL("pristine", ConditionFromHealth(1.f), EAftermathCondition::Pristine);
	UTEST_EQUAL("worn", ConditionFromHealth(0.6f), EAftermathCondition::Worn);
	UTEST_EQUAL("damaged", ConditionFromHealth(0.4f), EAftermathCondition::Damaged);
	UTEST_EQUAL("badly damaged", ConditionFromHealth(0.1f), EAftermathCondition::BadlyDamaged);
	UTEST_EQUAL("ruined", ConditionFromHealth(0.f), EAftermathCondition::Ruined);

	FAftermathGrid G(6, 6);
	G.AddAnywhere(Make(TEXT("Knife")));
	G.AddAnywhere(Make(TEXT("Apple")));
	UTEST_EQUAL("grid weight", G.TotalWeight(), Def(TEXT("Knife"))->WeightGrams + Def(TEXT("Apple"))->WeightGrams);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
