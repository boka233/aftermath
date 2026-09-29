// Stage 2 survival and item-action tests. Survival is simulated by calling Simulate() directly with
// large steps, so minutes of game time run in milliseconds.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Interaction/AftermathPickup.h"
#include "Items/AftermathInventoryComponent.h"
#include "Items/AftermathItemActions.h"
#include "Items/AftermathItemDatabase.h"
#include "Movement/AftermathMovementComponent.h"
#include "Player/AftermathCharacter.h"
#include "Survival/AftermathSurvivalComponent.h"
#include "Survival/AftermathSurvivalMath.h"

namespace AftermathSurvTest
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
			UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
			FActorSpawnParameters P;
			P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AStaticMeshActor* Floor = World->SpawnActor<AStaticMeshActor>(FVector(0, 0, -25.f), FRotator::ZeroRotator, P);
			Floor->SetMobility(EComponentMobility::Movable);
			Floor->GetStaticMeshComponent()->SetStaticMesh(Cube);
			// 300m square: long enough for ~10s of sprinting in a straight line without running off the edge.
			Floor->SetActorScale3D(FVector(300.f, 300.f, 0.5f));
			return World != nullptr;
		}

		~FWorld()
		{
			Wrapper.EndPlayInTestWorld();
			Wrapper.DestroyTestWorld(true);
		}

		AAftermathCharacter* Survivor(const FVector& At = FVector(0, 0, 92.f))
		{
			FActorSpawnParameters P;
			P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AAftermathCharacter* C = World->SpawnActor<AAftermathCharacter>(At, FRotator::ZeroRotator, P);
			if (C)
			{
				C->GetCharacterMovement()->bRunPhysicsWithNoController = true;
				C->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
				C->GetSurvival()->SetRandomSeed(1234);
			}
			return C;
		}

		void Tick(float Seconds, float Dt = 1.f / 60.f)
		{
			for (float T = 0.f; T < Seconds; T += Dt)
			{
				Wrapper.TickTestWorld(Dt);
			}
		}
	};

	/** Run the survival simulation for Seconds of game time in 1s steps. */
	void Simulate(UAftermathSurvivalComponent* S, float Seconds)
	{
		for (float T = 0.f; T < Seconds; T += 1.f)
		{
			S->Simulate(1.f);
		}
	}

	FAftermathItemInstance Make(FName Id, int32 Count = 1)
	{
		const FAftermathItemDef* D = UAftermathItemDatabase::Get()->Find(Id);
		return D ? AftermathItems::MakeInstance(*D, Count) : FAftermathItemInstance();
	}

	/** Mild weather where a dressed survivor stays comfortable. */
	FAftermathEnvironment Mild()
	{
		FAftermathEnvironment E;
		E.AmbientWarmth = 0.2f;
		return E;
	}
}

using namespace AftermathSurvTest;

// ---------------------------------------------------------------------------
// Pure math
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAftermathSurvMathTest, "Aftermath.Survival.Math", AftermathSurvTest::Flags)
bool FAftermathSurvMathTest::RunTest(const FString& Parameters)
{
	const FAftermathSurvivalTuning T;
	using namespace AftermathSurvival;

	UTEST_EQUAL("no fall damage from a step", FallDamage(T, 500.f), 0.f);
	UTEST_TRUE("some damage from a 1 storey drop", FallDamage(T, 1000.f) > 0.f && FallDamage(T, 1000.f) < 30.f);
	UTEST_TRUE("lethal from high", FallDamage(T, T.FallLethalSpeed) >= T.MaxHealth);
	UTEST_TRUE("monotonic", FallDamage(T, 1200.f) < FallDamage(T, 1500.f));

	UTEST_EQUAL("light load full speed", WeightSpeedFactor(T, 5.f), 1.f);
	UTEST_NEARLY_EQUAL("max load slow", WeightSpeedFactor(T, T.WeightMaxKg), 0.6f, 0.001f);
	UTEST_NEARLY_EQUAL("over max clamps", WeightSpeedFactor(T, 100.f), 0.6f, 0.001f);

	FAftermathEnvironment Cold;
	Cold.AmbientWarmth = -1.f;
	FAftermathEnvironment ColdRain = Cold;
	ColdRain.Rain = 1.f;
	UTEST_TRUE("naked in cold loses heat", HeatDelta(T, Cold, 0.f, 1.f, 0.5f) < 0.f);
	UTEST_TRUE("rain makes it worse", HeatDelta(T, ColdRain, 0.f, 1.f, 0.5f) < HeatDelta(T, Cold, 0.f, 1.f, 0.5f));
	UTEST_TRUE("clothes help", HeatDelta(T, Cold, 0.8f, 1.f, 0.3f) > HeatDelta(T, Cold, 0.f, 1.f, 0.3f));
	UTEST_TRUE("running warms", HeatDelta(T, Cold, 0.f, 4.f, 0.3f) > HeatDelta(T, Cold, 0.f, 1.f, 0.3f));

	UTEST_TRUE("sprint burns more than idle", ActivityFromSpeed(T, 650.f) > ActivityFromSpeed(T, 0.f));
	return true;
}

// ---------------------------------------------------------------------------
// Survival component
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAftermathSurvStarveTest, "Aftermath.Survival.HungerThirstKill", AftermathSurvTest::Flags)
bool FAftermathSurvStarveTest::RunTest(const FString& Parameters)
{
	FWorld W;
	UTEST_TRUE("world", W.Init(*this));
	AAftermathCharacter* C = W.Survivor();
	UAftermathSurvivalComponent* S = C->GetSurvival();
	C->GetInventory()->GiveStartingLoadout();
	S->SetEnvironment(Mild());

	const float Energy0 = S->GetVitals().Energy;
	const float Water0 = S->GetVitals().Water;
	Simulate(S, 60.f);
	UTEST_TRUE("energy drains", S->GetVitals().Energy < Energy0);
	UTEST_TRUE("water drains faster than energy (relative)", Water0 - S->GetVitals().Water > 0.f);
	UTEST_TRUE("alive and healthy after a minute", !S->IsDead() && S->GetVitals().Health > 99.f);

	// Water runs out first; then health drains until death.
	for (int32 i = 0; i < 20000 && !S->IsDead(); ++i)
	{
		S->Simulate(1.f);
	}
	UTEST_TRUE("eventually dies of thirst", S->IsDead());
	UTEST_TRUE("character knows it's dead", C->IsDead());
	UTEST_TRUE("died with no water", S->GetVitals().Water <= 0.f);
	// Starting clothes were dropped on death.
	int32 OnGround = 0;
	for (TActorIterator<AAftermathPickup> It(W.World); It; ++It) { ++OnGround; }
	UTEST_EQUAL("clothes on the ground", OnGround, 3);
	UTEST_EQUAL("nothing left on the body", C->GetInventory()->CountItems(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAftermathSurvBleedTest, "Aftermath.Survival.BleedingAndBandage", AftermathSurvTest::Flags)
bool FAftermathSurvBleedTest::RunTest(const FString& Parameters)
{
	FWorld W;
	UTEST_TRUE("world", W.Init(*this));
	AAftermathCharacter* C = W.Survivor();
	UAftermathSurvivalComponent* S = C->GetSurvival();
	UAftermathInventoryComponent* Inv = C->GetInventory();
	Inv->GiveStartingLoadout(); // 2 rags
	S->SetEnvironment(Mild());
	S->SetForceRolls(true, false); // rags never infect in this test

	S->AddCut(2);
	UTEST_TRUE("bleeding", S->IsBleeding());
	const float Blood0 = S->GetVitals().Blood;
	Simulate(S, 30.f);
	UTEST_TRUE("losing blood", S->GetVitals().Blood < Blood0 - 400.f);

	const int32 Rag = Inv->FindFirstUid(TEXT("Rag"));
	UTEST_EQUAL("bandage one cut", AftermathItemActions::Use(*C, Rag), EAftermathActionResult::Ok);
	UTEST_EQUAL("one cut left", S->GetVitals().Cuts, 1);
	UTEST_EQUAL("one rag left", Inv->CountOf(TEXT("Rag")), 1);
	UTEST_EQUAL("bandage the other", AftermathItemActions::Use(*C, Inv->FindFirstUid(TEXT("Rag"))), EAftermathActionResult::Ok);
	UTEST_TRUE("stopped", !S->IsBleeding());
	UTEST_EQUAL("rags used up", Inv->CountOf(TEXT("Rag")), 0);

	// Nothing to treat: the item is not consumed.
	Inv->AddItem(Make(TEXT("Bandage"), 2));
	UTEST_EQUAL("no wound", AftermathItemActions::Use(*C, Inv->FindFirstUid(TEXT("Bandage"))), EAftermathActionResult::NothingToTreat);
	UTEST_EQUAL("bandages kept", Inv->CountOf(TEXT("Bandage")), 2);

	// Fed and not bleeding: blood regenerates.
	const float Blood1 = S->GetVitals().Blood;
	Simulate(S, 60.f);
	UTEST_TRUE("blood regenerates", S->GetVitals().Blood > Blood1);

	// Dirty rags can infect.
	S->AddCut(1);
	Inv->AddItem(Make(TEXT("Rag"), 1));
	S->SetForceRolls(true, true);
	AftermathItemActions::Use(*C, Inv->FindFirstUid(TEXT("Rag")));
	UTEST_TRUE("infected -> sick", S->GetVitals().bSick);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAftermathSurvBleedOutTest, "Aftermath.Survival.BleedOutKills", AftermathSurvTest::Flags)
bool FAftermathSurvBleedOutTest::RunTest(const FString& Parameters)
{
	FWorld W;
	UTEST_TRUE("world", W.Init(*this));
	AAftermathCharacter* C = W.Survivor();
	UAftermathSurvivalComponent* S = C->GetSurvival();
	S->SetEnvironment(Mild());
	S->AddCut(4);
	int32 Seconds = 0;
	for (; Seconds < 3000 && !S->IsDead(); ++Seconds)
	{
		S->Simulate(1.f);
	}
	UTEST_TRUE("bled out", S->IsDead());
	// 4 cuts * 8 ml/s from 5000 ml: about 2-3 minutes, not seconds and not an hour.
	UTEST_TRUE("takes minutes", Seconds > 60 && Seconds < 600);
	AddInfo(FString::Printf(TEXT("bled out after %d s"), Seconds));

	// Dead characters ignore further input.
	UTEST_EQUAL("dead can't use items", AftermathItemActions::Use(*C, 1), EAftermathActionResult::Dead);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAftermathSurvColdTest, "Aftermath.Survival.ColdClothesAndSickness", AftermathSurvTest::Flags)
bool FAftermathSurvColdTest::RunTest(const FString& Parameters)
{
	FAftermathEnvironment Cold;
	Cold.AmbientWarmth = -0.8f;
	Cold.Rain = 0.5f;

	// Naked in cold rain gets cold.
	float NakedHeat = 0.f;
	{
		FWorld W;
		UTEST_TRUE("world", W.Init(*this));
		AAftermathCharacter* C = W.Survivor();
		UAftermathSurvivalComponent* S = C->GetSurvival();
		S->SetEnvironment(Cold);
		S->SetForceRolls(true, false);
		Simulate(S, 300.f);
		NakedHeat = S->GetVitals().Heat;
		UTEST_TRUE("naked gets cold", NakedHeat < S->Tuning.ColdThreshold);
	}

	// Dressed warm stays comfortable.
	{
		FWorld W;
		UTEST_TRUE("world", W.Init(*this));
		AAftermathCharacter* C = W.Survivor();
		UAftermathSurvivalComponent* S = C->GetSurvival();
		UAftermathInventoryComponent* Inv = C->GetInventory();
		Inv->AddItem(Make(TEXT("Hoodie")));
		Inv->AddItem(Make(TEXT("Jeans")));
		Inv->AddItem(Make(TEXT("Sneakers")));
		S->SetEnvironment(Cold);
		S->SetForceRolls(true, false);
		Simulate(S, 300.f);
		UTEST_TRUE("dressed is warmer", S->GetVitals().Heat > NakedHeat + 0.1f);

		// Soaked clothes lose most of their warmth.
		for (EAftermathWearSlot Slot : { EAftermathWearSlot::Torso, EAftermathWearSlot::Legs })
		{
			const int32 Uid = Inv->GetSlot(Slot)->Item.Uid;
			Inv->FindItemMutable(Uid)->Wetness = 1.f;
		}
		const float DryInsulation = 0.35f + 0.2f + 0.05f;
		UTEST_TRUE("wet insulates badly", Inv->GetInsulation() < DryInsulation * 0.5f);
	}

	// Being cold can give you a cold; medicine helps, a cold passes on its own too.
	{
		FWorld W;
		UTEST_TRUE("world", W.Init(*this));
		AAftermathCharacter* C = W.Survivor();
		UAftermathSurvivalComponent* S = C->GetSurvival();
		UAftermathInventoryComponent* Inv = C->GetInventory();
		Inv->AddItem(Make(TEXT("TShirt")));
		S->SetEnvironment(Cold);
		S->SetForceRolls(true, true); // every roll succeeds
		Simulate(S, 200.f);
		UTEST_TRUE("caught a cold", S->GetVitals().bSick);

		Inv->AddItem(Make(TEXT("Tetracycline")));
		const int32 Pills = Inv->FindFirstUid(TEXT("Tetracycline"));
		UTEST_EQUAL("take pills", AftermathItemActions::Use(*C, Pills), EAftermathActionResult::Ok);
		UTEST_EQUAL("one dose used", Inv->FindItem(Pills)->Uses, 5);
		AftermathItemActions::Use(*C, Pills);
		UTEST_TRUE("cured after two doses", !S->GetVitals().bSick);
		UTEST_EQUAL("healthy people don't waste pills", AftermathItemActions::Use(*C, Pills), EAftermathActionResult::NothingToTreat);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAftermathSurvFallTest, "Aftermath.Survival.FallBreaksLegsAndSplint", AftermathSurvTest::Flags)
bool FAftermathSurvFallTest::RunTest(const FString& Parameters)
{
	FWorld W;
	UTEST_TRUE("world", W.Init(*this));
	AAftermathCharacter* C = W.Survivor();
	UAftermathSurvivalComponent* S = C->GetSurvival();
	UAftermathInventoryComponent* Inv = C->GetInventory();
	UAftermathMovementComponent* M = C->GetAftermathMovement();
	Inv->AddItem(Make(TEXT("Backpack")));
	W.Tick(0.3f);

	// A real fall: feet ~8m up (landing ~1260 cm/s: breaks legs, below the cut threshold) and let physics land it.
	C->SetActorLocation(FVector(0, 0, 900.f));
	M->SetMovementMode(MOVE_Falling);
	W.Tick(2.f);
	UTEST_TRUE("landed", M->IsMovingOnGround());
	UTEST_TRUE("hurt", S->GetVitals().Health < 100.f);
	UTEST_TRUE("legs broken", S->GetVitals().bBrokenLegs);
	UTEST_TRUE("alive", !S->IsDead());

	UTEST_TRUE("can't sprint on broken legs", !S->CanSprint());
	M->SetWantsSprint(true);
	for (int32 i = 0; i < 90; ++i)
	{
		C->AddMovementInput(FVector::ForwardVector, 1.f);
		W.Wrapper.TickTestWorld(1.f / 60.f);
	}
	const float BrokenSpeed = M->Velocity.Size2D();
	UTEST_TRUE("limping", BrokenSpeed < M->Speeds.Jog * 0.5f);

	Inv->AddItem(Make(TEXT("Splint")));
	UTEST_EQUAL("apply splint", AftermathItemActions::Use(*C, Inv->FindFirstUid(TEXT("Splint"))), EAftermathActionResult::Ok);
	UTEST_EQUAL("splint used", Inv->CountOf(TEXT("Splint")), 0);
	UTEST_TRUE("still no sprint", !S->CanSprint());
	for (int32 i = 0; i < 90; ++i)
	{
		C->AddMovementInput(FVector::ForwardVector, 1.f);
		W.Wrapper.TickTestWorld(1.f / 60.f);
	}
	UTEST_TRUE("faster with a splint", M->Velocity.Size2D() > BrokenSpeed + 20.f);
	M->SetWantsSprint(false);

	// Short hop does nothing.
	const float Hp = S->GetVitals().Health;
	S->OnLanded(600.f);
	UTEST_EQUAL("no damage from a hop", S->GetVitals().Health, Hp);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAftermathSurvWeightTest, "Aftermath.Survival.WeightSlowsAndBlocksSprint", AftermathSurvTest::Flags)
bool FAftermathSurvWeightTest::RunTest(const FString& Parameters)
{
	FWorld W;
	UTEST_TRUE("world", W.Init(*this));
	AAftermathCharacter* C = W.Survivor();
	UAftermathSurvivalComponent* S = C->GetSurvival();
	UAftermathInventoryComponent* Inv = C->GetInventory();
	UAftermathMovementComponent* M = C->GetAftermathMovement();
	Inv->AddItem(Make(TEXT("Backpack")));
	S->Simulate(0.1f);
	UTEST_TRUE("light", S->CanSprint());

	// 36 full water bottles (1.1 kg each) ~ 40 kg.
	for (int32 i = 0; i < 18; ++i)
	{
		Inv->AddItem(Make(TEXT("WaterBottle")));
	}
	Inv->AddItem(Make(TEXT("Hoodie"))); // worn -> 3x3 more room
	Inv->AddItem(Make(TEXT("Jeans")));
	for (int32 i = 0; i < 16; ++i)
	{
		Inv->AddItem(Make(TEXT("WaterBottle")));
	}
	S->Simulate(0.1f);
	AddInfo(FString::Printf(TEXT("carrying %.1f kg"), S->GetVitals().CarryKg));
	UTEST_TRUE("heavy", S->GetVitals().CarryKg > S->Tuning.WeightComfortKg + 10.f);
	UTEST_TRUE("slower", S->GetSpeedMultiplier() < 0.9f);

	W.Tick(0.2f);
	for (int32 i = 0; i < 90; ++i)
	{
		C->AddMovementInput(FVector::ForwardVector, 1.f);
		W.Wrapper.TickTestWorld(1.f / 60.f);
	}
	UTEST_TRUE("jogs slower when loaded", M->Velocity.Size2D() < M->Speeds.Jog * 0.95f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAftermathSurvStaminaTest, "Aftermath.Survival.SprintUsesStamina", AftermathSurvTest::Flags)
bool FAftermathSurvStaminaTest::RunTest(const FString& Parameters)
{
	FWorld W;
	UTEST_TRUE("world", W.Init(*this));
	AAftermathCharacter* C = W.Survivor();
	UAftermathSurvivalComponent* S = C->GetSurvival();
	UAftermathMovementComponent* M = C->GetAftermathMovement();
	S->SetEnvironment(Mild());
	W.Tick(0.2f);

	// Hold sprint + forward. The survival component ticks itself (10 Hz, server/authority) as the world
	// ticks, draining stamina until the character is exhausted. Don't also call Simulate() here: that
	// would drain twice as fast.
	auto RunFrames = [&](int32 Frames, float& OutMaxSpeed)
	{
		for (int32 i = 0; i < Frames; ++i)
		{
			C->AddMovementInput(FVector::ForwardVector, 1.f);
			W.Wrapper.TickTestWorld(1.f / 60.f);
			OutMaxSpeed = FMath::Max(OutMaxSpeed, (float)M->Velocity.Size2D());
		}
	};

	M->SetWantsSprint(true);
	float MaxSpeed = 0.f;
	int32 Frames = 0;
	for (; Frames < 60 * 15 && !S->GetVitals().bExhausted; Frames += 6)
	{
		RunFrames(6, MaxSpeed);
	}
	UTEST_TRUE("was sprinting", MaxSpeed > M->Speeds.Jog + 50.f);
	UTEST_TRUE("got exhausted", S->GetVitals().bExhausted);
	AddInfo(FString::Printf(TEXT("sprinted %.1f s"), Frames / 60.f));
	UTEST_TRUE("about 8 seconds of sprint", Frames > 60 * 6 && Frames < 60 * 11);

	// Still holding sprint: 1 more second isn't enough to recover, so it stays a jog (no flicker).
	float Ignored = 0.f;
	RunFrames(60, Ignored);
	UTEST_TRUE("still exhausted", S->GetVitals().bExhausted);
	UTEST_TRUE("still on the ground", M->IsMovingOnGround());
	UTEST_TRUE("dropped to a jog", M->Velocity.Size2D() < M->Speeds.Jog + 20.f);
	M->SetWantsSprint(false);

	// Rest recovers.
	Simulate(S, 10.f);
	UTEST_TRUE("recovered", S->GetVitals().Stamina > 50.f && !S->GetVitals().bExhausted);
	return true;
}

// ---------------------------------------------------------------------------
// Item actions
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAftermathActionFoodTest, "Aftermath.ItemActions.FoodDrinkAndCans", AftermathSurvTest::Flags)
bool FAftermathActionFoodTest::RunTest(const FString& Parameters)
{
	FWorld W;
	UTEST_TRUE("world", W.Init(*this));
	AAftermathCharacter* C = W.Survivor();
	UAftermathSurvivalComponent* S = C->GetSurvival();
	UAftermathInventoryComponent* Inv = C->GetInventory();
	Inv->AddItem(Make(TEXT("Backpack")));
	S->SetForceRolls(true, false);

	// Drinking a bottle takes 5 sips and leaves an empty bottle.
	Inv->AddItem(Make(TEXT("WaterBottle")));
	const int32 Bottle = Inv->FindFirstUid(TEXT("WaterBottle"));
	const float Water0 = S->GetVitals().Water;
	UTEST_EQUAL("sip", AftermathItemActions::Use(*C, Bottle), EAftermathActionResult::Ok);
	UTEST_NEARLY_EQUAL("water up", S->GetVitals().Water, Water0 + 200.f, 0.5f);
	for (int32 i = 0; i < 4; ++i) { AftermathItemActions::Use(*C, Bottle); }
	UTEST_EQUAL("bottle empty", Inv->FindItem(Bottle)->Uses, 0);
	UTEST_EQUAL("empty bottle can't be drunk", AftermathItemActions::Use(*C, Bottle), EAftermathActionResult::NotUsable);

	// A sealed can needs a tool.
	Inv->AddItem(Make(TEXT("CannedBeans")));
	const int32 Can = Inv->FindFirstUid(TEXT("CannedBeans"));
	UTEST_EQUAL("no tool", AftermathItemActions::Use(*C, Can), EAftermathActionResult::NeedsTool);
	UTEST_EQUAL("still sealed", Inv->CountOf(TEXT("CannedBeans")), 1);

	Inv->AddItem(Make(TEXT("Knife")));
	const int32 Knife = Inv->FindFirstUid(TEXT("Knife"));
	UTEST_EQUAL("open with knife (drag knife onto can)", AftermathItemActions::Combine(*C, Knife, Can), EAftermathActionResult::Ok);
	UTEST_EQUAL("sealed can gone", Inv->CountOf(TEXT("CannedBeans")), 0);
	UTEST_EQUAL("opened can", Inv->CountOf(TEXT("CannedBeansOpen")), 1);
	UTEST_TRUE("knife wore a bit", Inv->FindItem(Knife)->Health < 1.f);
	UTEST_EQUAL("same uid kept", Inv->FindItem(Can)->Def, FName(TEXT("CannedBeansOpen")));

	const float Energy0 = S->GetVitals().Energy;
	for (int32 i = 0; i < 4; ++i)
	{
		UTEST_EQUAL("eat", AftermathItemActions::Use(*C, Can), EAftermathActionResult::Ok);
	}
	UTEST_NEARLY_EQUAL("ate the whole can", S->GetVitals().Energy, FMath::Min(S->Tuning.MaxEnergy, Energy0 + 600.f), 1.f);
	UTEST_NULL("empty can discarded", Inv->FindItem(Can));

	// Raw meat usually poisons; charcoal cures it.
	S->SetForceRolls(true, true);
	Inv->AddItem(Make(TEXT("RawMeat")));
	AftermathItemActions::Use(*C, Inv->FindFirstUid(TEXT("RawMeat")));
	UTEST_TRUE("poisoned", S->GetVitals().bPoisoned);
	Inv->AddItem(Make(TEXT("Charcoal")));
	UTEST_EQUAL("take charcoal", AftermathItemActions::Use(*C, Inv->FindFirstUid(TEXT("Charcoal"))), EAftermathActionResult::Ok);
	UTEST_TRUE("cured", !S->GetVitals().bPoisoned);

	// Unusable things.
	UTEST_EQUAL("can't eat a knife", AftermathItemActions::Use(*C, Knife), EAftermathActionResult::NotUsable);
	UTEST_EQUAL("unknown uid", AftermathItemActions::Use(*C, 987654), EAftermathActionResult::NotFound);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAftermathActionCraftTest, "Aftermath.ItemActions.RagsAndMagazines", AftermathSurvTest::Flags)
bool FAftermathActionCraftTest::RunTest(const FString& Parameters)
{
	FWorld W;
	UTEST_TRUE("world", W.Init(*this));
	AAftermathCharacter* C = W.Survivor();
	UAftermathInventoryComponent* Inv = C->GetInventory();
	Inv->AddItem(Make(TEXT("Backpack")));
	Inv->AddItem(Make(TEXT("Knife")));
	const int32 Knife = Inv->FindFirstUid(TEXT("Knife"));

	// Torn shirt -> rags. A worn shirt must be taken off first.
	Inv->AddItem(Make(TEXT("TShirt"))); // worn
	const int32 WornShirt = Inv->GetSlot(EAftermathWearSlot::Torso)->Item.Uid;
	UTEST_EQUAL("can't tear worn clothes", AftermathItemActions::Combine(*C, Knife, WornShirt), EAftermathActionResult::NotUsable);
	FAftermathItemInstance Spare = Make(TEXT("TShirt"));
	Inv->AddItem(Spare); // torso taken -> goes into a cargo grid (the worn shirt's 2x2 pockets fit it)
	const int32 LooseShirt = Spare.Uid;
	FAftermathItemLocation SpareLoc;
	UTEST_NOT_NULL("spare shirt carried", Inv->FindItem(LooseShirt, &SpareLoc));
	UTEST_TRUE("spare shirt not worn", !SpareLoc.bWorn);
	UTEST_EQUAL("tear it", AftermathItemActions::Combine(*C, LooseShirt, Knife), EAftermathActionResult::Ok);
	UTEST_NULL("shirt gone", Inv->FindItem(LooseShirt));
	UTEST_EQUAL("2 rags from a 2x2 shirt", Inv->CountOf(TEXT("Rag")), 2);

	// Load a magazine: 40 rounds, capacity 15.
	Inv->AddItem(Make(TEXT("Mag9mm")));
	Inv->AddItem(Make(TEXT("Ammo9mm"), 40));
	const int32 Mag = Inv->FindFirstUid(TEXT("Mag9mm"));
	const int32 Ammo = Inv->FindFirstUid(TEXT("Ammo9mm"));
	UTEST_EQUAL("load (ammo onto mag)", AftermathItemActions::Combine(*C, Ammo, Mag), EAftermathActionResult::Ok);
	UTEST_EQUAL("mag full", Inv->FindItem(Mag)->Loaded, 15);
	UTEST_EQUAL("25 loose left", Inv->CountOf(TEXT("Ammo9mm")), 25);
	UTEST_EQUAL("full mag refuses more", AftermathItemActions::Combine(*C, Mag, Ammo), EAftermathActionResult::NoRoom);
	const int32 LoadedWeight = AftermathItems::InstanceWeight(*Inv->FindItem(Mag));

	UTEST_EQUAL("unload", AftermathItemActions::Unload(*C, Mag), EAftermathActionResult::Ok);
	UTEST_EQUAL("mag empty", Inv->FindItem(Mag)->Loaded, 0);
	UTEST_EQUAL("rounds back", Inv->CountOf(TEXT("Ammo9mm")), 40);
	UTEST_TRUE("empty mag lighter", AftermathItems::InstanceWeight(*Inv->FindItem(Mag)) < LoadedWeight);
	UTEST_EQUAL("unload empty", AftermathItemActions::Unload(*C, Mag), EAftermathActionResult::NothingToTreat);

	// Nonsense combos.
	UTEST_EQUAL("knife + mag", AftermathItemActions::Combine(*C, Knife, Mag), EAftermathActionResult::NotUsable);
	UTEST_EQUAL("self", AftermathItemActions::Combine(*C, Knife, Knife), EAftermathActionResult::Invalid);

	// Wear from cargo via Use.
	Inv->AddItem(Make(TEXT("Hoodie")));
	UTEST_TRUE("hoodie in backpack (torso taken)", Inv->GetSlot(EAftermathWearSlot::Torso)->Item.Uid == WornShirt);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
