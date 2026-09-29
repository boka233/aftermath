// Stage 5 loot economy in a test world: spawn points, restock to nominal, taken items get replaced,
// stale items get cleaned up, nothing spawns next to a player.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Interaction/AftermathPickup.h"
#include "Items/AftermathInventoryComponent.h"
#include "Player/AftermathCharacter.h"
#include "World/AftermathLootManager.h"
#include "World/AftermathLootSpawnPoint.h"

namespace AftermathLootWorldTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	AftermathLoot::FConfig Config()
	{
		AftermathLoot::FConfig C;
		C.RestockIntervalSeconds = 1e6f; // only explicit passes in the test
		C.RestockBatch = 50;
		C.MinPlayerDistance = 3000.f;
		AftermathLoot::FItemRule Beans;
		Beans.Id = TEXT("CannedBeans"); Beans.Nominal = 5; Beans.Max = 6; Beans.Tags = { TEXT("House") }; Beans.Lifetime = 100.f;
		AftermathLoot::FItemRule Rifle;
		Rifle.Id = TEXT("Rifle"); Rifle.Nominal = 2; Rifle.Max = 2; Rifle.Tags = { TEXT("Military") }; Rifle.Lifetime = 0.f;
		C.Items = { Beans, Rifle };
		return C;
	}

	int32 CountPickups(UWorld* W, FName Id)
	{
		int32 N = 0;
		for (TActorIterator<AAftermathPickup> It(W); It; ++It)
		{
			N += (!It->IsTaken() && It->GetItemId() == Id) ? 1 : 0;
		}
		return N;
	}
}

using namespace AftermathLootWorldTest;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAftermathLootWorldTest, "Aftermath.Loot.World", AftermathLootWorldTest::Flags)
bool FAftermathLootWorldTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Wrapper;
	UTEST_TRUE("world", Wrapper.CreateTestWorld(EWorldType::Game) && Wrapper.BeginPlayInTestWorld());
	UWorld* World = Wrapper.GetTestWorld();
	UAftermathLootManager* Loot = World->GetSubsystem<UAftermathLootManager>();
	UTEST_NOT_NULL("loot manager", Loot);
	Loot->SetConfig(Config());

	// 10 house points far away (X 100..109 m), 3 military points near where a player will stand.
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	for (int32 i = 0; i < 10; ++i)
	{
		World->SpawnActor<AAftermathLootSpawnPoint>(FVector(10000.f + i * 100.f, 0.f, 50.f), FRotator::ZeroRotator, P)->LootTag = TEXT("House");
	}
	for (int32 i = 0; i < 3; ++i)
	{
		World->SpawnActor<AAftermathLootSpawnPoint>(FVector(0.f, i * 100.f, 50.f), FRotator::ZeroRotator, P)->LootTag = TEXT("Military");
	}
	Wrapper.TickTestWorld(1.f / 30.f);
	UTEST_EQUAL("points registered", Loot->NumPoints(), 13);

	// A player stands on the military points: rifles must not pop into view.
	AAftermathCharacter* Player = World->SpawnActor<AAftermathCharacter>(FVector(0.f, 100.f, 92.f), FRotator::ZeroRotator, P);
	Player->GetCharacterMovement()->bRunPhysicsWithNoController = true;
	// Clothes give pockets to put a can in (a bare spawned character has nowhere to carry it).
	Player->GetInventory()->GiveStartingLoadout();
	Loot->RunPass();
	TestEqual("beans to nominal", CountPickups(World, TEXT("CannedBeans")), 5);
	TestEqual("no rifles next to the player", CountPickups(World, TEXT("Rifle")), 0);

	// Player walks away: rifles appear.
	Player->SetActorLocation(FVector(-50000.f, 0.f, 92.f));
	Loot->RunPass();
	TestEqual("rifles once nobody watches", CountPickups(World, TEXT("Rifle")), 2);
	TestEqual("beans stay at nominal", CountPickups(World, TEXT("CannedBeans")), 5);

	// Someone takes a can: the next pass replaces it (at another point).
	AAftermathPickup* Can = nullptr;
	for (TActorIterator<AAftermathPickup> It(World); It; ++It)
	{
		if (It->GetItemId() == TEXT("CannedBeans")) { Can = *It; break; }
	}
	UTEST_NOT_NULL("a can", Can);
	Player->SetActorLocation(Can->GetActorLocation() + FVector(0, 0, 92.f));
	Player->GetInventory()->ServerTakeFromGround(Can, EAftermathTakeMode::Auto, EAftermathWearSlot::None, 0, 0, false);
	Wrapper.TickTestWorld(1.f / 30.f);
	TestEqual("taken", CountPickups(World, TEXT("CannedBeans")), 4);
	Player->SetActorLocation(FVector(-50000.f, 0.f, 92.f));
	Loot->RunPass();
	TestEqual("restocked after being taken", CountPickups(World, TEXT("CannedBeans")), 5);

	// Players dropping extra cans above nominal: the economy doesn't add more.
	for (int32 i = 0; i < 3; ++i)
	{
		World->SpawnActor<AAftermathPickup>(FVector(-40000.f, i * 100.f, 20.f), FRotator::ZeroRotator, P)->SetItemById(TEXT("CannedBeans"));
	}
	Loot->RunPass();
	TestEqual("no restock above nominal", CountPickups(World, TEXT("CannedBeans")), 8);

	// Time passes past the beans' lifetime: stale economy cans are cleaned (dropped ones are the player's).
	// World time clamps each frame to MaxUndilatedFrameTime (0.4 s), so step in 0.25 s frames.
	const double Before = World->GetTimeSeconds();
	while (World->GetTimeSeconds() - Before < 101.0)
	{
		Wrapper.TickTestWorld(0.25f);
	}
	Loot->RunPass();
	TestTrue("stale economy items cleaned", Loot->LastCleaned() >= 5);
	TestEqual("rifles (lifetime 0) never cleaned", CountPickups(World, TEXT("Rifle")), 2);

	Wrapper.EndPlayInTestWorld();
	Wrapper.DestroyTestWorld(true);
	return true;
}

#endif
