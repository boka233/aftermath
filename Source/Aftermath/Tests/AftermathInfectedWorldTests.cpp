// Stage 4 infected world tests: real infected + survivors in a test world (no navmesh, so infected walk
// straight at their goal; walls and closed doors still block them).

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#include "Combat/AftermathCombatComponent.h"
#include "Combat/AftermathNoise.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Infected/AftermathInfected.h"
#include "Infected/AftermathInfectedManager.h"
#include "Interaction/AftermathDoor.h"
#include "Items/AftermathInventoryComponent.h"
#include "Items/AftermathItemDatabase.h"
#include "Player/AftermathCharacter.h"
#include "Survival/AftermathSurvivalComponent.h"

namespace AftermathInfectedWorldTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	using S = EAftermathInfectedState;

	struct FWorld
	{
		FTestWorldWrapper Wrapper;
		UWorld* World = nullptr;
		UStaticMesh* Cube = nullptr;
		UAftermathInfectedManager* Manager = nullptr;

		bool Init(FAutomationTestBase& Test)
		{
			if (!Wrapper.CreateTestWorld(EWorldType::Game) || !Wrapper.BeginPlayInTestWorld())
			{
				Wrapper.ForwardErrorMessages(&Test);
				return false;
			}
			World = Wrapper.GetTestWorld();
			Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
			// Large enough for the far (sleeping) update-rate test.
			Box(FVector(0, 0, -25.f), FVector(60000.f, 60000.f, 50.f));
			Manager = World->GetSubsystem<UAftermathInfectedManager>();
			return World != nullptr && Manager != nullptr;
		}

		~FWorld()
		{
			Wrapper.EndPlayInTestWorld();
			Wrapper.DestroyTestWorld(true);
		}

		AStaticMeshActor* Box(const FVector& Center, const FVector& Size)
		{
			FActorSpawnParameters P;
			P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AStaticMeshActor* A = World->SpawnActor<AStaticMeshActor>(Center, FRotator::ZeroRotator, P);
			A->SetMobility(EComponentMobility::Movable);
			A->GetStaticMeshComponent()->SetStaticMesh(Cube);
			A->SetActorScale3D(Size / 100.f);
			A->GetStaticMeshComponent()->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
			return A;
		}

		AAftermathCharacter* Survivor(const FVector& Loc, float Yaw = 0.f)
		{
			FActorSpawnParameters P;
			P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AAftermathCharacter* C = World->SpawnActor<AAftermathCharacter>(Loc, FRotator(0.f, Yaw, 0.f), P);
			C->GetCharacterMovement()->bRunPhysicsWithNoController = true;
			C->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
			return C;
		}

		AAftermathInfected* Infected(const FVector& Loc, float Yaw = 0.f, EAftermathInfectedType Type = EAftermathInfectedType::Civilian)
		{
			const FTransform Xf(FRotator(0.f, Yaw, 0.f), Loc);
			AAftermathInfected* I = World->SpawnActorDeferred<AAftermathInfected>(AAftermathInfected::StaticClass(), Xf, nullptr, nullptr,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			I->SetInfectedType(Type);
			// Deterministic facing: no idle wandering.
			I->bAllowWander = false;
			I->FinishSpawning(Xf);
			I->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
			return I;
		}

		void Tick(float Seconds)
		{
			for (float T = 0.f; T < Seconds; T += 1.f / 30.f)
			{
				Wrapper.TickTestWorld(1.f / 30.f);
			}
		}

		/** Tick until Pred is true (or Seconds pass). Returns whether it became true. */
		template <typename F>
		bool TickUntil(float Seconds, F&& Pred)
		{
			for (float T = 0.f; T < Seconds; T += 1.f / 30.f)
			{
				Wrapper.TickTestWorld(1.f / 30.f);
				if (Pred())
				{
					return true;
				}
			}
			return Pred();
		}
	};

	void ArmPistol(AAftermathCharacter* Who, int32 Rounds)
	{
		const FAftermathItemDef* D = UAftermathItemDatabase::FindDef(TEXT("Pistol"));
		if (!D)
		{
			return;
		}
		FAftermathItemInstance G = AftermathItems::MakeInstance(*D);
		G.AttachedMag = TEXT("Mag9mm");
		G.AttachedMagRounds = Rounds;
		Who->GetInventory()->AddItem(G);
	}

	FAftermathShotOutcome ShootAt(AAftermathCharacter* Who, const FVector& AimAt)
	{
		const FRotator R = (AimAt - Who->GetInteractEyeLocation()).Rotation();
		Who->SetActorRotation(FRotator(0.f, R.Yaw, 0.f));
		Who->SetRemoteViewPitch(R.Pitch);
		const FVector Eye = Who->GetInteractEyeLocation();
		Who->GetCombat()->SetAiming(true);
		return Who->GetCombat()->ServerHandleFire(Eye, (AimAt - Eye).GetSafeNormal(), 7, Who->GetWorld()->GetTimeSeconds());
	}
}

using namespace AftermathInfectedWorldTest;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAftermathInfectedSightRoarTest, "Aftermath.Infected.World.SightAndRoar", AftermathInfectedWorldTest::Flags)
bool FAftermathInfectedSightRoarTest::RunTest(const FString& Parameters)
{
	FWorld W;
	UTEST_TRUE("world", W.Init(*this));

	// A faces the player 8 m ahead; B is 15 m behind A facing away (can't see the player).
	AAftermathCharacter* Player = W.Survivor(FVector(800.f, 0, 92.f), 180.f);
	AAftermathInfected* A = W.Infected(FVector(0, 0, 92.f), 0.f);
	AAftermathInfected* B = W.Infected(FVector(-1500.f, 0, 92.f), 180.f);
	W.Tick(0.2f);

	UTEST_TRUE("A notices and chases", W.TickUntil(2.f, [&] { return A->GetState() == S::Chase || A->GetState() == S::Attack; }));
	TestTrue("A targets the player", A->GetTarget() == Player);
	UTEST_TRUE("A's roar brings B", W.TickUntil(1.f, [&] { return B->GetState() != S::Idle; }));
	TestTrue("B heads toward the player", B->HasInterestPoint() && FVector::Dist2D(B->GetInterestPoint(), Player->GetActorLocation()) < 300.f);

	// A behind the player, out of view, doesn't see a crouched (quiet, still) player 10 m away.
	AAftermathCharacter* Quiet = W.Survivor(FVector(0, 5000.f, 92.f));
	AAftermathInfected* C = W.Infected(FVector(0, 6000.f, 92.f), 90.f); // faces +Y, away from Quiet
	W.Tick(2.f);
	TestEqual("C behind never notices", C->GetState(), S::Idle);
	TestTrue("C has no awareness", C->GetAwareness() < 0.05f);
	(void)Quiet;
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAftermathInfectedAttackTest, "Aftermath.Infected.World.ChaseAndAttack", AftermathInfectedWorldTest::Flags)
bool FAftermathInfectedAttackTest::RunTest(const FString& Parameters)
{
	FWorld W;
	UTEST_TRUE("world", W.Init(*this));

	AAftermathCharacter* Player = W.Survivor(FVector(600.f, 0, 92.f), 180.f);
	AAftermathInfected* I = W.Infected(FVector(0, 0, 92.f), 0.f);
	const float StartHealth = Player->GetSurvival()->GetVitals().Health;
	W.Tick(0.2f);

	UTEST_TRUE("runs up and attacks", W.TickUntil(5.f, [&] { return I->GetState() == S::Attack; }));
	TestTrue("closed the gap", FVector::Dist2D(I->GetActorLocation(), Player->GetActorLocation()) <= AftermathInfectedMath::AttackReach + 30.f);
	UTEST_TRUE("swings land", W.TickUntil(4.f, [&] { return Player->GetSurvival()->GetVitals().Health < StartHealth; }));

	// Walk out of reach: it follows (back to chase).
	Player->SetActorLocation(Player->GetActorLocation() + FVector(800.f, 0, 0));
	TestTrue("follows the player", W.TickUntil(3.f, [&] { return I->GetState() == S::Chase; }));

	// Player dies: the infected loses its target and searches.
	Player->GetSurvival()->ApplyDamage(1000.f);
	TestTrue("player dead", Player->IsDead());
	TestTrue("stops chasing a corpse", W.TickUntil(2.f, [&] { return I->GetState() == S::Search; }));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAftermathInfectedShotTest, "Aftermath.Infected.World.ShotAndKilled", AftermathInfectedWorldTest::Flags)
bool FAftermathInfectedShotTest::RunTest(const FString& Parameters)
{
	FWorld W;
	UTEST_TRUE("world", W.Init(*this));

	AAftermathCharacter* Shooter = W.Survivor(FVector(0, 0, 92.f));
	ArmPistol(Shooter, 15);
	// Both face away from the shooter so they don't start running before we shoot.
	AAftermathInfected* Civ = W.Infected(FVector(1500.f, -300.f, 92.f), 0.f, EAftermathInfectedType::Civilian);
	AAftermathInfected* Sol = W.Infected(FVector(1500.f, 300.f, 92.f), 0.f, EAftermathInfectedType::Soldier);
	W.Tick(0.3f);

	// Head: 40 * 3 = 120 kills a civilian (100) outright.
	const FAftermathShotOutcome O1 = ShootAt(Shooter, Civ->GetActorLocation() + FVector(0, 0, 80.f));
	UTEST_TRUE("bullet hit the civilian", O1.bHitCharacter && O1.Victim.Get() == Civ);
	TestEqual("head zone", O1.Zone, EAftermathHitZone::Head);
	TestTrue("civilian killed", Civ->IsDead());

	// The soldier's helmet halves it: survives one head shot, and now knows who shot it.
	W.Tick(0.3f);
	const FAftermathShotOutcome O2 = ShootAt(Shooter, Sol->GetActorLocation() + FVector(0, 0, 80.f));
	UTEST_TRUE("bullet hit the soldier", O2.bHitCharacter && O2.Victim.Get() == Sol);
	TestFalse("helmet saved it", Sol->IsDead());
	TestTrue("soldier took damage", Sol->GetHealth() < AftermathInfectedMath::GetType(EAftermathInfectedType::Soldier).Health);
	TestTrue("turns on the shooter", W.TickUntil(1.f, [&] { return Sol->GetState() == S::Chase && Sol->GetTarget() == Shooter; }));

	// Dead bodies don't stop bullets meant for the living, and can't be hit again.
	TestFalse("corpse not hittable", AftermathCombat::IsHittable(Civ));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAftermathInfectedDoorTest, "Aftermath.Infected.World.HearsThroughWallNoDoors", AftermathInfectedWorldTest::Flags)
bool FAftermathInfectedDoorTest::RunTest(const FString& Parameters)
{
	FWorld W;
	UTEST_TRUE("world", W.Init(*this));

	// Wall along Y at X=500 with a 1 m doorway at Y 0..100, closed by a door.
	W.Box(FVector(500.f, -500.f, 150.f), FVector(20.f, 1000.f, 300.f));
	W.Box(FVector(500.f, 600.f, 150.f), FVector(20.f, 1000.f, 300.f));
	W.Box(FVector(500.f, 50.f, 260.f), FVector(20.f, 100.f, 80.f)); // lintel
	FActorSpawnParameters DP;
	DP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AAftermathDoor* Door = W.World->SpawnActor<AAftermathDoor>(FVector(500.f, 0.f, 0.f), FRotator(0.f, 90.f, 0.f), DP);
	UTEST_TRUE("door", Door != nullptr && !Door->IsOpen());

	AAftermathCharacter* Player = W.Survivor(FVector(1000.f, 50.f, 92.f), 180.f);
	AAftermathInfected* I = W.Infected(FVector(0.f, 50.f, 92.f), 0.f);
	W.Tick(1.f);
	TestEqual("can't see through the door", I->GetState(), S::Idle);

	// A gunshot on the other side: heard through the wall (muffled), comes to look.
	W.World->GetSubsystem<UAftermathNoiseSubsystem>()->Report(EAftermathNoiseKind::Gunshot, Player->GetInteractEyeLocation(), 15000.f, Player);
	UTEST_TRUE("heard it", W.TickUntil(1.f, [&] { return I->GetState() == S::Alert; }));

	W.Tick(6.f);
	TestTrue("the closed door stops it", I->GetActorLocation().X < 480.f);
	TestFalse("never opened the door", Door->IsOpen());

	// Door opens (swinging toward the player's side, away from the infected pressed against it):
	// now it sees the player and gets through.
	Door->SetOpen(true, FVector(300.f, 50.f, 0.f));
	W.Tick(1.f);
	W.Manager->ForceThinkAll();
	UTEST_TRUE("sees through the open doorway", W.TickUntil(3.f, [&] { return I->GetState() == S::Chase || I->GetState() == S::Attack; }));
	TestTrue("gets through", W.TickUntil(6.f, [&] { return I->GetActorLocation().X > 520.f; }));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAftermathInfectedLodWorldTest, "Aftermath.Infected.World.UpdateRate", AftermathInfectedWorldTest::Flags)
bool FAftermathInfectedLodWorldTest::RunTest(const FString& Parameters)
{
	FWorld W;
	UTEST_TRUE("world", W.Init(*this));

	W.Survivor(FVector(0, 0, 92.f));
	// Near faces away (so it stays idle), mid is 120 m away, far is 250 m away.
	AAftermathInfected* Near = W.Infected(FVector(-1000.f, 0, 92.f), 180.f);
	AAftermathInfected* Mid = W.Infected(FVector(12000.f, 0, 92.f));
	AAftermathInfected* Far = W.Infected(FVector(0, 25000.f, 92.f));
	W.Tick(0.2f);
	const int32 N0 = Near->ThinkCount, M0 = Mid->ThinkCount;
	W.Tick(3.f);

	const int32 NearThinks = Near->ThinkCount - N0;
	const int32 MidThinks = Mid->ThinkCount - M0;
	AddInfo(FString::Printf(TEXT("thinks in 3s: near=%d mid=%d far=%d"), NearThinks, MidThinks, Far->ThinkCount));
	TestTrue("near thinks ~10 Hz", NearThinks >= 25);
	TestTrue("mid thinks ~1 Hz", MidThinks >= 2 && MidThinks <= 5);
	TestTrue("far sleeps", Far->IsSleeping());
	TestEqual("far never thinks", Far->ThinkCount, 0);
	TestFalse("near awake", Near->IsSleeping());

	// A gunshot reaching the sleeper wakes it.
	W.World->GetSubsystem<UAftermathNoiseSubsystem>()->Report(EAftermathNoiseKind::Gunshot, FVector(0, 20000.f, 150.f), 20000.f, nullptr);
	W.Tick(0.2f);
	TestFalse("woken by noise", Far->IsSleeping());
	TestTrue("and alerted", Far->GetState() == S::Alert);
	return true;
}

#endif
