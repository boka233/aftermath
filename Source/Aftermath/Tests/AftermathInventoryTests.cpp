// Stage 2 inventory world tests: server-side inventory rules on a real character in a test world.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Interaction/AftermathPickup.h"
#include "Items/AftermathInventoryComponent.h"
#include "Items/AftermathItemDatabase.h"
#include "Player/AftermathCharacter.h"

namespace AftermathInvTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	struct FWorld
	{
		FTestWorldWrapper Wrapper;
		UWorld* World = nullptr;

		bool Init(FAutomationTestBase& Test)
		{
			if (!Wrapper.CreateTestWorld(EWorldType::Game) || !Wrapper.BeginPlayInTestWorld())
			{
				Wrapper.ForwardErrorMessages(&Test);
				return false;
			}
			World = Wrapper.GetTestWorld();
			// Floor so dropped items have ground to land on.
			UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
			FActorSpawnParameters P;
			P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AStaticMeshActor* Floor = World->SpawnActor<AStaticMeshActor>(FVector(0, 0, -25.f), FRotator::ZeroRotator, P);
			Floor->SetMobility(EComponentMobility::Movable);
			Floor->GetStaticMeshComponent()->SetStaticMesh(Cube);
			Floor->SetActorScale3D(FVector(40.f, 40.f, 0.5f));
			return World != nullptr;
		}

		~FWorld()
		{
			Wrapper.EndPlayInTestWorld();
			Wrapper.DestroyTestWorld(true);
		}

		AAftermathCharacter* Survivor()
		{
			FActorSpawnParameters P;
			P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			return World->SpawnActor<AAftermathCharacter>(FVector(0, 0, 92.f), FRotator::ZeroRotator, P);
		}

		AAftermathPickup* Pickup(FName Id, int32 Count, const FVector& Loc = FVector(80.f, 0.f, 10.f))
		{
			FActorSpawnParameters P;
			P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AAftermathPickup* A = World->SpawnActor<AAftermathPickup>(Loc, FRotator::ZeroRotator, P);
			A->SetItemById(Id, Count);
			return A;
		}

		int32 CountPickups(FName Id = NAME_None)
		{
			int32 N = 0;
			for (TActorIterator<AAftermathPickup> It(World); It; ++It)
			{
				if (!It->IsActorBeingDestroyed() && (Id.IsNone() || It->GetItemId() == Id))
				{
					++N;
				}
			}
			return N;
		}
	};

	FAftermathItemInstance Make(FName Id, int32 Count = 1)
	{
		const FAftermathItemDef* D = UAftermathItemDatabase::Get()->Find(Id);
		return D ? AftermathItems::MakeInstance(*D, Count) : FAftermathItemInstance();
	}
}

using namespace AftermathInvTest;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAftermathInvCapacityTest, "Aftermath.Inventory.CapacityComesFromClothes", AftermathInvTest::Flags)
bool FAftermathInvCapacityTest::RunTest(const FString& Parameters)
{
	FWorld W;
	UTEST_TRUE("world", W.Init(*this));
	AAftermathCharacter* C = W.Survivor();
	UAftermathInventoryComponent* Inv = C->GetInventory();
	UTEST_NOT_NULL("inventory", Inv);

	// Naked: nowhere to put a knife.
	UTEST_EQUAL("naked can't carry", Inv->AddItem(Make(TEXT("Knife"))), 1);
	UTEST_EQUAL("nothing carried", Inv->CountItems(), 0);

	// Clothes are worn automatically into their slot.
	UTEST_EQUAL("shirt worn", Inv->AddItem(Make(TEXT("TShirt"))), 0);
	UTEST_TRUE("torso slot filled", !Inv->GetSlot(EAftermathWearSlot::Torso)->IsEmpty());
	const FAftermathGrid* Pockets = Inv->GetGrid(EAftermathWearSlot::Torso);
	UTEST_TRUE("shirt pockets 2x2", Pockets && Pockets->Width == 2 && Pockets->Height == 2);

	UTEST_EQUAL("knife fits (1x2)", Inv->AddItem(Make(TEXT("Knife"))), 0);
	UTEST_EQUAL("second knife fits", Inv->AddItem(Make(TEXT("Knife"))), 0);
	UTEST_EQUAL("pockets full", Inv->AddItem(Make(TEXT("Apple"))), 1);

	// Second shirt can't be worn (slot taken) and doesn't fit in the full pockets.
	UTEST_EQUAL("second shirt refused", Inv->AddItem(Make(TEXT("TShirt"))), 1);

	// Backpack adds a 6x6 grid.
	UTEST_EQUAL("backpack worn", Inv->AddItem(Make(TEXT("Backpack"))), 0);
	UTEST_EQUAL("apple fits now", Inv->AddItem(Make(TEXT("Apple"))), 0);
	UTEST_EQUAL("apple went to backpack", Inv->GetGrid(EAftermathWearSlot::Back)->CountOf(TEXT("Apple")), 1);
	UTEST_EQUAL("second shirt fits in backpack", Inv->AddItem(Make(TEXT("TShirt"))), 0);

	// Weight and warmth.
	UTEST_TRUE("weight counted", Inv->TotalWeightGrams() > 2000);
	UTEST_TRUE("shirt + backpack insulation", Inv->GetInsulation() > 0.1f && Inv->GetInsulation() < 0.2f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAftermathInvMoveTest, "Aftermath.Inventory.MoveEquipUnequip", AftermathInvTest::Flags)
bool FAftermathInvMoveTest::RunTest(const FString& Parameters)
{
	FWorld W;
	UTEST_TRUE("world", W.Init(*this));
	AAftermathCharacter* C = W.Survivor();
	UAftermathInventoryComponent* Inv = C->GetInventory();

	Inv->AddItem(Make(TEXT("Jeans")));    // 2x3 pockets
	Inv->AddItem(Make(TEXT("Backpack"))); // 6x6

	FAftermathItemInstance Knife = Make(TEXT("Knife"));
	Inv->AddItem(Knife);
	FAftermathItemLocation Loc;
	UTEST_NOT_NULL("knife carried", Inv->FindItem(Knife.Uid, &Loc));
	UTEST_EQUAL("in jeans", Loc.Slot, EAftermathWearSlot::Legs);

	UTEST_TRUE("move to backpack corner", Inv->MoveItem(Knife.Uid, EAftermathWearSlot::Back, 5, 4, false));
	Inv->FindItem(Knife.Uid, &Loc);
	UTEST_EQUAL("now in backpack", Loc.Slot, EAftermathWearSlot::Back);
	UTEST_TRUE("out of bounds refused", !Inv->MoveItem(Knife.Uid, EAftermathWearSlot::Back, 5, 5, false));
	UTEST_TRUE("rotate in place", Inv->MoveItem(Knife.Uid, EAftermathWearSlot::Back, 4, 5, true));
	UTEST_TRUE("no such container", !Inv->MoveItem(Knife.Uid, EAftermathWearSlot::Torso, 0, 0, false));
	UTEST_TRUE("unknown uid", !Inv->MoveItem(123456789, EAftermathWearSlot::Back, 0, 0, false));

	// A hoodie in the backpack can be equipped (torso empty) and gives its own grid.
	FAftermathItemInstance Hoodie = Make(TEXT("Hoodie"));
	Inv->AddItem(Hoodie); // torso free -> worn immediately
	UTEST_TRUE("hoodie worn", Inv->GetGrid(EAftermathWearSlot::Torso) != nullptr);

	// Take it off into the backpack: allowed because its pockets are empty.
	UTEST_TRUE("unequip hoodie", Inv->Unequip(EAftermathWearSlot::Torso, EAftermathWearSlot::Back, 0, 0, false));
	UTEST_TRUE("torso empty", Inv->GetSlot(EAftermathWearSlot::Torso)->IsEmpty());
	UTEST_TRUE("equip it again", Inv->Equip(Hoodie.Uid));

	// With something in its pockets it can't go into another container.
	Inv->MoveItem(Knife.Uid, EAftermathWearSlot::Torso, 0, 0, false);
	UTEST_TRUE("full hoodie can't be packed", !Inv->Unequip(EAftermathWearSlot::Torso, EAftermathWearSlot::Back, 0, 0, false));
	UTEST_TRUE("can't unequip into itself", !Inv->Unequip(EAftermathWearSlot::Back, EAftermathWearSlot::Back, 0, 0, false));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAftermathInvStackTest, "Aftermath.Inventory.StackSplitMergeConsume", AftermathInvTest::Flags)
bool FAftermathInvStackTest::RunTest(const FString& Parameters)
{
	FWorld W;
	UTEST_TRUE("world", W.Init(*this));
	AAftermathCharacter* C = W.Survivor();
	UAftermathInventoryComponent* Inv = C->GetInventory();
	Inv->AddItem(Make(TEXT("Backpack")));

	FAftermathItemInstance Ammo = Make(TEXT("Ammo9mm"), 40);
	Inv->AddItem(Ammo);
	UTEST_TRUE("split 10", Inv->Split(Ammo.Uid, 10));
	UTEST_EQUAL("still 40 total", Inv->CountOf(TEXT("Ammo9mm")), 40);
	UTEST_EQUAL("two stacks", Inv->GetGrid(EAftermathWearSlot::Back)->Entries.Num(), 2);

	const int32 Other = Inv->GetGrid(EAftermathWearSlot::Back)->Entries[1].Item.Uid;
	UTEST_TRUE("merge back", Inv->Merge(Other, Ammo.Uid));
	UTEST_EQUAL("one stack again", Inv->GetGrid(EAftermathWearSlot::Back)->Entries.Num(), 1);

	UTEST_TRUE("consume 15", Inv->ConsumeCount(Ammo.Uid, 15));
	UTEST_EQUAL("25 left", Inv->CountOf(TEXT("Ammo9mm")), 25);
	UTEST_TRUE("can't consume more than there is", !Inv->ConsumeCount(Ammo.Uid, 26));
	UTEST_TRUE("consume the rest", Inv->ConsumeCount(Ammo.Uid, 25));
	UTEST_NULL("stack gone", Inv->FindItem(Ammo.Uid));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAftermathInvGroundTest, "Aftermath.Inventory.DropAndTakeFromGround", AftermathInvTest::Flags)
bool FAftermathInvGroundTest::RunTest(const FString& Parameters)
{
	FWorld W;
	UTEST_TRUE("world", W.Init(*this));
	AAftermathCharacter* C = W.Survivor();
	UAftermathInventoryComponent* Inv = C->GetInventory();
	Inv->AddItem(Make(TEXT("TShirt")));

	// Take a bottle into the shirt; it keeps its state (uses) through drop and pick-up.
	AAftermathPickup* Bottle = W.Pickup(TEXT("WaterBottle"), 1);
	UTEST_TRUE("take bottle", Inv->TakeFromGround(Bottle, EAftermathTakeMode::Auto));
	const int32 BottleUid = Inv->FindFirstUid(TEXT("WaterBottle"));
	UTEST_TRUE("bottle carried", BottleUid != 0);
	Inv->FindItemMutable(BottleUid)->Uses = 2;

	UTEST_TRUE("drop bottle", Inv->Drop(BottleUid));
	UTEST_EQUAL("bottle on the ground", W.CountPickups(TEXT("WaterBottle")), 1);
	AAftermathPickup* Dropped = nullptr;
	for (TActorIterator<AAftermathPickup> It(W.World); It; ++It)
	{
		if (!It->IsActorBeingDestroyed() && It->GetItemId() == TEXT("WaterBottle")) { Dropped = *It; }
	}
	UTEST_NOT_NULL("dropped pickup", Dropped);
	UTEST_EQUAL("uses kept on ground", Dropped->GetItem().Uses, 2);
	UTEST_TRUE("take it back", Inv->TakeFromGround(Dropped, EAftermathTakeMode::Auto));
	UTEST_EQUAL("uses kept after pickup", Inv->FindItem(Inv->FindFirstUid(TEXT("WaterBottle")))->Uses, 2);

	// Stacks merge on pickup. Shirt is 2x2: bottle takes 2 cells, so 2 cells are left.
	// 4 rags -> one stack; 5 more -> tops that stack up to 6 and puts the other 3 in the last cell.
	AAftermathPickup* Rags = W.Pickup(TEXT("Rag"), 4);
	UTEST_TRUE("take rags", Inv->TakeFromGround(Rags, EAftermathTakeMode::Auto));
	AAftermathPickup* MoreRags = W.Pickup(TEXT("Rag"), 5);
	UTEST_TRUE("take more rags", Inv->TakeFromGround(MoreRags, EAftermathTakeMode::Auto));
	UTEST_EQUAL("all 9 rags carried", Inv->CountOf(TEXT("Rag")), 9);
	UTEST_EQUAL("in two stacks", Inv->GetGrid(EAftermathWearSlot::Torso)->Entries.Num(), 3);
	UTEST_EQUAL("no rags left on the ground", W.CountPickups(TEXT("Rag")), 0);

	// Out of reach.
	AAftermathPickup* Far = W.Pickup(TEXT("Knife"), 1, FVector(1000.f, 0, 10.f));
	AddExpectedMessage(TEXT("InteractRejected reason=range"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1, false);
	UTEST_TRUE("far refused", !Inv->TakeFromGround(Far, EAftermathTakeMode::Auto));
	UTEST_EQUAL("far knife still there", W.CountPickups(TEXT("Knife")), 1);

	// A pickup that's already been taken can't be taken again.
	AAftermathPickup* Apple = W.Pickup(TEXT("Apple"), 1);
	Inv->AddItem(Make(TEXT("Backpack")));
	UTEST_TRUE("take apple", Inv->TakeFromGround(Apple, EAftermathTakeMode::Auto));
	UTEST_TRUE("apple twice refused", !Inv->TakeFromGround(Apple, EAftermathTakeMode::Auto));
	UTEST_EQUAL("one apple", Inv->CountOf(TEXT("Apple")), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAftermathInvPartialTest, "Aftermath.Inventory.PartialStackPickup", AftermathInvTest::Flags)
bool FAftermathInvPartialTest::RunTest(const FString& Parameters)
{
	FWorld W;
	UTEST_TRUE("world", W.Init(*this));
	AAftermathCharacter* C = W.Survivor();
	UAftermathInventoryComponent* Inv = C->GetInventory();
	Inv->AddItem(Make(TEXT("TShirt"))); // 4 cells

	// Fill 3 cells with knives/apple so exactly one 1x1 cell is free.
	Inv->AddItem(Make(TEXT("Knife")));  // 1x2
	Inv->AddItem(Make(TEXT("Apple")));  // 1x1
	UTEST_EQUAL("one free cell", Inv->CountItems(), 3);

	// A full 50-round stack fills the last free cell; a second pile has nowhere to go.
	AAftermathPickup* Ammo = W.Pickup(TEXT("Ammo9mm"), 50);
	UTEST_TRUE("take ammo", Inv->TakeFromGround(Ammo, EAftermathTakeMode::Auto));
	UTEST_EQUAL("50 carried", Inv->CountOf(TEXT("Ammo9mm")), 50);
	UTEST_TRUE("pickup gone", Ammo->IsTaken());

	AAftermathPickup* More = W.Pickup(TEXT("Ammo9mm"), 30);
	UTEST_TRUE("no room at all", !Inv->TakeFromGround(More, EAftermathTakeMode::Auto));
	UTEST_EQUAL("still 30 on ground", More->GetItem().Count, 30);

	// Spend 20 so the stack has room; now a partial take moves 20 and leaves 10.
	Inv->ConsumeCount(Inv->FindFirstUid(TEXT("Ammo9mm")), 20);
	UTEST_TRUE("partial take", Inv->TakeFromGround(More, EAftermathTakeMode::Auto));
	UTEST_EQUAL("stack full again", Inv->CountOf(TEXT("Ammo9mm")), 50);
	UTEST_EQUAL("10 left on ground", More->GetItem().Count, 10);
	UTEST_TRUE("ground pickup still there", !More->IsTaken());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAftermathInvBackpackDropTest, "Aftermath.Inventory.BackpackKeepsContentsWhenDropped", AftermathInvTest::Flags)
bool FAftermathInvBackpackDropTest::RunTest(const FString& Parameters)
{
	FWorld W;
	UTEST_TRUE("world", W.Init(*this));
	AAftermathCharacter* A = W.Survivor();
	UAftermathInventoryComponent* Inv = A->GetInventory();

	FAftermathItemInstance Pack = Make(TEXT("Backpack"));
	Inv->AddItem(Pack);
	Inv->AddItem(Make(TEXT("CannedBeans")));
	Inv->AddItem(Make(TEXT("Ammo9mm"), 30));
	UTEST_EQUAL("pack contents", Inv->GetGrid(EAftermathWearSlot::Back)->Entries.Num(), 2);

	UTEST_TRUE("drop pack", Inv->Drop(Pack.Uid));
	UTEST_EQUAL("nothing carried", Inv->CountItems(), 0);

	AAftermathPickup* OnGround = nullptr;
	for (TActorIterator<AAftermathPickup> It(W.World); It; ++It)
	{
		if (!It->IsActorBeingDestroyed() && It->GetItemId() == TEXT("Backpack")) { OnGround = *It; }
	}
	UTEST_NOT_NULL("pack on ground", OnGround);
	UTEST_EQUAL("contents stayed inside", OnGround->GetCargo().Entries.Num(), 2);

	// Wearing it again brings the contents back.
	UTEST_TRUE("take pack", Inv->TakeFromGround(OnGround, EAftermathTakeMode::Auto));
	UTEST_EQUAL("beans back", Inv->CountOf(TEXT("CannedBeans")), 1);
	UTEST_EQUAL("ammo back", Inv->CountOf(TEXT("Ammo9mm")), 30);

	// Death drops everything worn (with contents) on the ground.
	Inv->AddItem(Make(TEXT("TShirt")));
	Inv->DropAll();
	UTEST_EQUAL("naked after death", Inv->CountItems(), 0);
	UTEST_EQUAL("two containers on ground", W.CountPickups(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAftermathInvLoadoutTest, "Aftermath.Inventory.StartingLoadout", AftermathInvTest::Flags)
bool FAftermathInvLoadoutTest::RunTest(const FString& Parameters)
{
	FWorld W;
	UTEST_TRUE("world", W.Init(*this));
	AAftermathCharacter* C = W.Survivor();
	UAftermathInventoryComponent* Inv = C->GetInventory();
	Inv->GiveStartingLoadout();

	UTEST_TRUE("shirt", !Inv->GetSlot(EAftermathWearSlot::Torso)->IsEmpty());
	UTEST_TRUE("jeans", !Inv->GetSlot(EAftermathWearSlot::Legs)->IsEmpty());
	UTEST_TRUE("shoes", !Inv->GetSlot(EAftermathWearSlot::Feet)->IsEmpty());
	UTEST_TRUE("no backpack", Inv->GetSlot(EAftermathWearSlot::Back)->IsEmpty());
	UTEST_EQUAL("2 rags", Inv->CountOf(TEXT("Rag")), 2);
	UTEST_EQUAL("an apple", Inv->CountOf(TEXT("Apple")), 1);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
