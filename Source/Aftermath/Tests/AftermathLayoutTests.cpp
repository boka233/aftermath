// Inventory panel layout and hit-testing: pure geometry, no rendering. Guarantees that what the HUD
// draws and where clicks land agree, at several resolutions.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#include "Engine/World.h"
#include "Items/AftermathInventoryComponent.h"
#include "Items/AftermathItemDatabase.h"
#include "Player/AftermathCharacter.h"
#include "UI/AftermathInventoryLayout.h"

namespace AftermathLayoutTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	FAftermathItemInstance Make(FName Id, int32 Count = 1)
	{
		const FAftermathItemDef* D = UAftermathItemDatabase::Get()->Find(Id);
		return D ? AftermathItems::MakeInstance(*D, Count) : FAftermathItemInstance();
	}

	bool Overlaps(const FAftermathInvRect& A, const FAftermathInvRect& B)
	{
		return A.X < B.X + B.W && B.X < A.X + A.W && A.Y < B.Y + B.H && B.Y < A.Y + A.H;
	}
}

using namespace AftermathLayoutTest;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAftermathLayoutTest, "Aftermath.UI.InventoryLayoutAndHitTest", AftermathLayoutTest::Flags)
bool FAftermathLayoutTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Wrapper;
	UTEST_TRUE("world", Wrapper.CreateTestWorld(EWorldType::Game) && Wrapper.BeginPlayInTestWorld());
	UWorld* World = Wrapper.GetTestWorld();
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AAftermathCharacter* C = World->SpawnActor<AAftermathCharacter>(FVector(0, 0, 100.f), FRotator::ZeroRotator, P);
	UAftermathInventoryComponent* Inv = C->GetInventory();
	Inv->AddItem(Make(TEXT("TShirt")));
	Inv->AddItem(Make(TEXT("Jeans")));
	Inv->AddItem(Make(TEXT("Backpack")));
	FAftermathItemInstance Knife = Make(TEXT("Knife"));
	Inv->AddItem(Knife);
	Inv->MoveItem(Knife.Uid, EAftermathWearSlot::Back, 2, 3, false);

	const TArray<int32> Hotbar = { Knife.Uid, 0, 0, 0, 0, 0, 0, 0, 0 };
	const TArray<AAftermathPickup*> NoPickups;

	struct FRes { float W, H; };
	// 640x360 is what the scripted network test clients use; the panel must still fit above the hotbar.
	for (const FRes Res : { FRes{ 640, 360 }, FRes{ 1280, 720 }, FRes{ 1920, 1080 }, FRes{ 2560, 1440 } })
	{
		FAftermathInventoryLayout L;
		L.Build(Res.W, Res.H, *Inv, NoPickups, true);
		const FString Tag = FString::Printf(TEXT("%.0fx%.0f"), Res.W, Res.H);

		// Structure.
		UTEST_EQUAL(*(Tag + TEXT(" slots: hands head torso vest legs feet back")), L.Slots.Num(), 7);
		UTEST_EQUAL(*(Tag + TEXT(" grids: torso, legs, back")), L.Grids.Num(), 3);
		UTEST_EQUAL(*(Tag + TEXT(" hotbar")), L.Hotbar.Num(), 9);

		// Everything on screen and nothing overlapping.
		TArray<FAftermathInvRect> All;
		for (const FAftermathInvSlotView& S : L.Slots) { All.Add(S.Rect); }
		for (const FAftermathInvGridView& G : L.Grids) { All.Add(G.Rect); }
		for (const FAftermathInvRect& R : L.Hotbar) { All.Add(R); }
		All.Add(L.NearbyPanel);
		for (int32 i = 0; i < All.Num(); ++i)
		{
			const FAftermathInvRect& R = All[i];
			if (R.X < 0 || R.Y < 0 || R.X + R.W > Res.W || R.Y + R.H > Res.H)
			{
				AddError(FString::Printf(TEXT("%s: rect %d off screen (%.0f,%.0f %.0fx%.0f)"), *Tag, i, R.X, R.Y, R.W, R.H));
			}
			for (int32 j = i + 1; j < All.Num(); ++j)
			{
				if (Overlaps(R, All[j]))
				{
					AddError(FString::Printf(TEXT("%s: rect %d overlaps %d"), *Tag, i, j));
				}
			}
		}

		// Hit testing: centre of backpack cell (2,4) is the knife (1x2 at 2,3).
		const FAftermathInvGridView* Back = L.FindGrid(EAftermathWearSlot::Back);
		UTEST_NOT_NULL(*(Tag + TEXT(" back grid")), Back);
		const float CX = Back->Rect.X + 2.5f * L.Cell;
		const float CY = Back->Rect.Y + 4.5f * L.Cell;
		FAftermathInvHit Hit = L.HitTest(CX, CY, *Inv, Hotbar);
		UTEST_TRUE(*(Tag + TEXT(" hits knife")), Hit.Kind == FAftermathInvHit::EKind::GridCell && Hit.Uid == Knife.Uid
			&& Hit.CellX == 2 && Hit.CellY == 4 && Hit.Container == EAftermathWearSlot::Back);

		// Empty cell.
		Hit = L.HitTest(Back->Rect.X + 0.5f * L.Cell, Back->Rect.Y + 0.5f * L.Cell, *Inv, Hotbar);
		UTEST_TRUE(*(Tag + TEXT(" empty cell")), Hit.Kind == FAftermathInvHit::EKind::GridCell && Hit.Uid == 0);

		// Equip slot for legs shows the worn jeans.
		const FAftermathInvSlotView* Legs = L.Slots.FindByPredicate([](const FAftermathInvSlotView& S) { return S.Slot == EAftermathWearSlot::Legs; });
		Hit = L.HitTest(Legs->Rect.X + 5.f, Legs->Rect.Y + 5.f, *Inv, Hotbar);
		UTEST_TRUE(*(Tag + TEXT(" legs slot")), Hit.Kind == FAftermathInvHit::EKind::EquipSlot && Hit.Uid == Inv->GetSlot(EAftermathWearSlot::Legs)->Item.Uid);

		// Hotbar slot 1 holds the knife.
		Hit = L.HitTest(L.Hotbar[0].X + 2.f, L.Hotbar[0].Y + 2.f, *Inv, Hotbar);
		UTEST_TRUE(*(Tag + TEXT(" hotbar 1")), Hit.Kind == FAftermathInvHit::EKind::Hotbar && Hit.HotbarIndex == 0 && Hit.Uid == Knife.Uid);

		// Far corner: nothing.
		Hit = L.HitTest(Res.W - 2.f, 2.f, *Inv, Hotbar);
		UTEST_TRUE(*(Tag + TEXT(" nothing")), Hit.Kind == FAftermathInvHit::EKind::None);
	}

	// Closed panel: only the hotbar is laid out.
	FAftermathInventoryLayout Closed;
	Closed.Build(1920, 1080, *Inv, NoPickups, false);
	UTEST_EQUAL("closed: no grids", Closed.Grids.Num(), 0);
	UTEST_EQUAL("closed: hotbar still there", Closed.Hotbar.Num(), 9);

	Wrapper.EndPlayInTestWorld();
	Wrapper.DestroyTestWorld(true);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
