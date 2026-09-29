// Automation tests for stage 1: stance speeds, vault feasibility/trajectory, interaction reach.
// Run: UnrealEditor-Cmd.exe Aftermath.uproject -ExecCmds="Automation RunTests Aftermath.; Quit" -unattended -nullrhi
//
// World tests build an empty game world, spawn obstacles and a character without a controller,
// then tick it manually. Movement runs through the real UAftermathMovementComponent code paths.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Interaction/AftermathDoor.h"
#include "Interaction/AftermathPickup.h"
#include "Items/AftermathInventoryComponent.h"
#include "Items/AftermathItemDatabase.h"
#include "Movement/AftermathMovementComponent.h"
#include "Movement/AftermathMovementMath.h"
#include "Player/AftermathCharacter.h"

namespace AftermathTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	/** Empty game world + helpers. Destroyed on scope exit. */
	struct FWorld
	{
		FTestWorldWrapper Wrapper;
		UWorld* World = nullptr;
		UStaticMesh* Cube = nullptr;

		bool Init(FAutomationTestBase& Test)
		{
			if (!Wrapper.CreateTestWorld(EWorldType::Game) || !Wrapper.BeginPlayInTestWorld())
			{
				Wrapper.ForwardErrorMessages(&Test);
				return false;
			}
			World = Wrapper.GetTestWorld();
			Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
			return World && Cube;
		}

		~FWorld()
		{
			Wrapper.EndPlayInTestWorld();
			Wrapper.DestroyTestWorld(true);
		}

		/** Static box with its centre at Center and full extents Size (cm). */
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

		/** 40m square floor with its top at Z=0. */
		void Floor()
		{
			Box(FVector(0.f, 0.f, -25.f), FVector(4000.f, 4000.f, 50.f));
		}

		/** Character standing on the floor at XY, facing +X. */
		AAftermathCharacter* Survivor(const FVector2D& XY)
		{
			FActorSpawnParameters P;
			P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AAftermathCharacter* C = World->SpawnActor<AAftermathCharacter>(FVector(XY.X, XY.Y, 92.f), FRotator::ZeroRotator, P);
			if (C)
			{
				// No controller in tests; still simulate movement.
				C->GetCharacterMovement()->bRunPhysicsWithNoController = true;
				C->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
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

	/** Hold forward input for Seconds. */
	void RunForward(FWorld& W, AAftermathCharacter* C, float Seconds, float Dt = 1.f / 60.f)
	{
		for (float T = 0.f; T < Seconds; T += Dt)
		{
			C->AddMovementInput(FVector::ForwardVector, 1.f);
			W.Wrapper.TickTestWorld(Dt);
		}
	}
}

using namespace AftermathTest;

// ---------------------------------------------------------------------------
// Pure logic
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAftermathSpeedTableTest, "Aftermath.Movement.SpeedTable", AftermathTest::Flags)
bool FAftermathSpeedTableTest::RunTest(const FString& Parameters)
{
	const FAftermathSpeedConfig S;
	using AftermathMath::ComputeMaxGroundSpeed;

	UTEST_EQUAL("jog", ComputeMaxGroundSpeed(S, EAftermathStance::Standing, false, false, true), S.Jog);
	UTEST_EQUAL("walk", ComputeMaxGroundSpeed(S, EAftermathStance::Standing, false, true, true), S.Walk);
	UTEST_EQUAL("sprint forward", ComputeMaxGroundSpeed(S, EAftermathStance::Standing, true, false, true), S.Sprint);
	UTEST_EQUAL("sprint sideways is jog", ComputeMaxGroundSpeed(S, EAftermathStance::Standing, true, false, false), S.Jog);
	UTEST_EQUAL("sprint beats walk", ComputeMaxGroundSpeed(S, EAftermathStance::Standing, true, true, true), S.Sprint);
	UTEST_EQUAL("crouch", ComputeMaxGroundSpeed(S, EAftermathStance::Crouched, false, false, true), S.Crouch);
	UTEST_EQUAL("crouch walk", ComputeMaxGroundSpeed(S, EAftermathStance::Crouched, false, true, true), S.CrouchWalk);
	UTEST_EQUAL("no sprint crouched", ComputeMaxGroundSpeed(S, EAftermathStance::Crouched, true, false, true), S.Crouch);
	UTEST_EQUAL("prone", ComputeMaxGroundSpeed(S, EAftermathStance::Prone, true, true, true), S.Prone);

	UTEST_TRUE("ordering", S.Prone < S.CrouchWalk && S.CrouchWalk < S.Walk && S.Walk < S.Crouch && S.Crouch < S.Jog && S.Jog < S.Sprint);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAftermathInteractRangeTest, "Aftermath.Interaction.RangeMath", AftermathTest::Flags)
bool FAftermathInteractRangeTest::RunTest(const FString& Parameters)
{
	using AftermathMath::IsWithinInteractRange;
	const FVector Eye(0.f, 0.f, 150.f);

	UTEST_TRUE("inside", IsWithinInteractRange(Eye, Eye + FVector(200.f, 0.f, 0.f), 220.f, 0.f));
	UTEST_TRUE("on the edge", IsWithinInteractRange(Eye, Eye + FVector(220.f, 0.f, 0.f), 220.f, 0.f));
	UTEST_TRUE("outside without tolerance", !IsWithinInteractRange(Eye, Eye + FVector(250.f, 0.f, 0.f), 220.f, 0.f));
	UTEST_TRUE("inside with tolerance", IsWithinInteractRange(Eye, Eye + FVector(250.f, 0.f, 0.f), 220.f, 60.f));
	UTEST_TRUE("far is rejected", !IsWithinInteractRange(Eye, Eye + FVector(1000.f, 0.f, 0.f), 220.f, 60.f));
	UTEST_TRUE("3D distance counts", !IsWithinInteractRange(Eye, Eye + FVector(200.f, 0.f, 200.f), 220.f, 0.f));
	UTEST_TRUE("negative range clamps", !IsWithinInteractRange(Eye, Eye + FVector(1.f, 0.f, 0.f), -500.f, 0.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAftermathVaultPathTest, "Aftermath.Movement.VaultPath", AftermathTest::Flags)
bool FAftermathVaultPathTest::RunTest(const FString& Parameters)
{
	// Wall front face 60cm ahead, 30cm thick, top at 100cm; standing capsule r=35 h=90; flat landing.
	const FVector Start(0.f, 0.f, 90.f);
	const FAftermathVaultPath P = FAftermathVaultPath::Build(Start, FVector::ForwardVector, 60.f, 90.f, 100.f, 90.f, 35.f, 90.f, 8.f);

	UTEST_TRUE("valid", P.IsValid());
	UTEST_EQUAL("peak clears wall top", P.PeakZ, 100.f + 90.f + 8.f);
	UTEST_TRUE("rise ends before the wall", P.RiseEnd + 35.f <= 60.f);
	UTEST_TRUE("hold ends past the back face", P.HoldEnd - 35.f >= 90.f);
	UTEST_TRUE("lands beyond hold", P.Length >= P.HoldEnd);

	UTEST_TRUE("starts at start", P.Evaluate(0.f).Equals(Start, 0.01f));
	const FVector End = P.Evaluate(P.TotalPathLength());
	UTEST_TRUE("ends on landing", FMath::IsNearlyEqual(End.X, P.Length, 0.01f) && FMath::IsNearlyEqual(End.Z, 90.f, 0.01f));
	UTEST_TRUE("clamped past end", P.Evaluate(P.TotalPathLength() * 2.f).Equals(End, 0.01f));

	// Anywhere the capsule overlaps the wall horizontally, its bottom must be above the wall top.
	const int32 Steps = 200;
	for (int32 i = 0; i <= Steps; ++i)
	{
		const FVector Pt = P.Evaluate(P.TotalPathLength() * i / Steps);
		const bool bOverWall = Pt.X + 35.f > 60.f && Pt.X - 35.f < 90.f;
		if (bOverWall && Pt.Z - 90.f < 100.f)
		{
			AddError(FString::Printf(TEXT("capsule clips wall at step %d: x=%.1f bottom=%.1f"), i, Pt.X, Pt.Z - 90.f));
			return false;
		}
	}

	// Monotonic forward progress.
	float LastX = -1.f;
	for (int32 i = 0; i <= Steps; ++i)
	{
		const float X = P.Evaluate(P.TotalPathLength() * i / Steps).X;
		UTEST_TRUE("forward progress", X >= LastX - 0.001f);
		LastX = X;
	}
	return true;
}

// ---------------------------------------------------------------------------
// World: stance
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAftermathStanceWorldTest, "Aftermath.Movement.StanceCapsuleAndSpeed", AftermathTest::Flags)
bool FAftermathStanceWorldTest::RunTest(const FString& Parameters)
{
	FWorld W;
	UTEST_TRUE("world", W.Init(*this));
	W.Floor();
	AAftermathCharacter* C = W.Survivor(FVector2D(0.f, 0.f));
	UTEST_NOT_NULL("character", C);
	UAftermathMovementComponent* M = C->GetAftermathMovement();
	UTEST_NOT_NULL("custom movement component", M);
	W.Tick(0.3f);

	UTEST_EQUAL("starts standing", C->GetStance(), EAftermathStance::Standing);
	RunForward(W, C, 1.5f);
	UTEST_NEARLY_EQUAL("jog speed", (float)M->Velocity.Size2D(), M->Speeds.Jog, 5.f);

	M->SetWantsSprint(true);
	RunForward(W, C, 1.0f);
	UTEST_NEARLY_EQUAL("sprint speed", (float)M->Velocity.Size2D(), M->Speeds.Sprint, 5.f);
	M->SetWantsSprint(false);

	C->ToggleCrouchStance();
	RunForward(W, C, 1.0f);
	UTEST_EQUAL("crouched", C->GetStance(), EAftermathStance::Crouched);
	UTEST_NEARLY_EQUAL("crouch half height", C->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight(), M->GetCrouchedHalfHeight(), 0.5f);
	UTEST_NEARLY_EQUAL("crouch speed", (float)M->Velocity.Size2D(), M->Speeds.Crouch, 5.f);
	UTEST_TRUE("bIsCrouched set for replication", C->bIsCrouched);

	C->ToggleProneStance();
	RunForward(W, C, 1.5f);
	UTEST_EQUAL("prone", C->GetStance(), EAftermathStance::Prone);
	UTEST_NEARLY_EQUAL("prone half height", C->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight(), M->ProneHalfHeight, 0.5f);
	UTEST_NEARLY_EQUAL("prone speed", (float)M->Velocity.Size2D(), M->Speeds.Prone, 5.f);
	UTEST_TRUE("prone flag published", C->IsReplicatedProne());
	UTEST_TRUE("still on the ground", M->IsMovingOnGround());
	UTEST_TRUE("feet on floor", FMath::Abs(C->GetActorLocation().Z - M->ProneHalfHeight) < 5.f);

	// Prone -> crouch directly.
	C->ToggleCrouchStance();
	W.Tick(0.2f);
	UTEST_EQUAL("prone to crouch", C->GetStance(), EAftermathStance::Crouched);
	UTEST_TRUE("prone flag cleared", !C->IsReplicatedProne());

	// Stand back up.
	C->ToggleCrouchStance();
	W.Tick(0.2f);
	UTEST_EQUAL("stand", C->GetStance(), EAftermathStance::Standing);
	UTEST_TRUE("bIsCrouched cleared", !C->bIsCrouched);

	// Sprint input stands you up from prone.
	C->ToggleProneStance();
	W.Tick(0.2f);
	UTEST_EQUAL("prone again", C->GetStance(), EAftermathStance::Prone);
	C->SetSprintInput(true);
	W.Tick(0.2f);
	UTEST_EQUAL("sprint stands up", C->GetStance(), EAftermathStance::Standing);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAftermathStanceBlockedTest, "Aftermath.Movement.StandUpBlockedUnderLowCeiling", AftermathTest::Flags)
bool FAftermathStanceBlockedTest::RunTest(const FString& Parameters)
{
	FWorld W;
	UTEST_TRUE("world", W.Init(*this));
	W.Floor();
	AAftermathCharacter* C = W.Survivor(FVector2D(0.f, 0.f));
	UTEST_NOT_NULL("character", C);
	UAftermathMovementComponent* M = C->GetAftermathMovement();
	W.Tick(0.2f);

	C->ToggleProneStance();
	W.Tick(0.3f);
	UTEST_EQUAL("prone", C->GetStance(), EAftermathStance::Prone);

	// Ceiling 100cm above ground over the character: prone (70) fits, crouch (120) and stand (180) don't.
	W.Box(FVector(0.f, 0.f, 110.f), FVector(400.f, 400.f, 20.f));
	W.Tick(0.1f);

	C->ToggleProneStance(); // request stand
	W.Tick(0.5f);
	UTEST_EQUAL("still prone under ceiling", C->GetStance(), EAftermathStance::Prone);
	UTEST_TRUE("prone flag kept", C->IsReplicatedProne());

	// Crawl out and it stands up on its own (intent is still "stand").
	RunForward(W, C, 4.5f);
	W.Tick(0.3f);
	UTEST_EQUAL("stood up once clear", C->GetStance(), EAftermathStance::Standing);
	UTEST_TRUE("moved out", C->GetActorLocation().X > 250.f);
	return true;
}

// ---------------------------------------------------------------------------
// World: vault
// ---------------------------------------------------------------------------

namespace AftermathTest
{
	/** Wall 30cm thick across the path, front face at X=FrontX, height H. */
	void Wall(FWorld& W, float FrontX, float H)
	{
		W.Box(FVector(FrontX + 15.f, 0.f, H * 0.5f), FVector(30.f, 400.f, H));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAftermathVaultQueryTest, "Aftermath.Movement.VaultQuery", AftermathTest::Flags)
bool FAftermathVaultQueryTest::RunTest(const FString& Parameters)
{
	struct FCase { float Height; bool bExpect; const TCHAR* Name; };
	const FCase Cases[] = {
		{ 25.f,  false, TEXT("too low") },
		{ 60.f,  true,  TEXT("60cm") },
		{ 100.f, true,  TEXT("100cm") },
		{ 125.f, true,  TEXT("125cm") },
		{ 180.f, false, TEXT("180cm too high") },
	};

	for (const FCase& Case : Cases)
	{
		FWorld W;
		UTEST_TRUE("world", W.Init(*this));
		W.Floor();
		Wall(W, 80.f, Case.Height);
		AAftermathCharacter* C = W.Survivor(FVector2D(0.f, 0.f));
		UTEST_NOT_NULL("character", C);
		W.Tick(0.2f);

		const FAftermathVaultQuery Q = C->GetAftermathMovement()->QueryVault();
		if (Q.bCanVault != Case.bExpect)
		{
			AddError(FString::Printf(TEXT("%s: expected %d got %d (%s, height %.1f)"),
				Case.Name, Case.bExpect, Q.bCanVault, *Q.FailReason, Q.ObstacleHeight));
		}
	}

	// Nothing in front.
	{
		FWorld W;
		UTEST_TRUE("world", W.Init(*this));
		W.Floor();
		AAftermathCharacter* C = W.Survivor(FVector2D(0.f, 0.f));
		W.Tick(0.2f);
		UTEST_TRUE("open ground", !C->GetAftermathMovement()->QueryVault().bCanVault);
	}

	// Too deep (a 2m block).
	{
		FWorld W;
		UTEST_TRUE("world", W.Init(*this));
		W.Floor();
		W.Box(FVector(80.f + 100.f, 0.f, 50.f), FVector(200.f, 400.f, 100.f));
		AAftermathCharacter* C = W.Survivor(FVector2D(0.f, 0.f));
		W.Tick(0.2f);
		UTEST_TRUE("deep block rejected", !C->GetAftermathMovement()->QueryVault().bCanVault);
	}

	// Low ceiling above the wall (no headroom).
	{
		FWorld W;
		UTEST_TRUE("world", W.Init(*this));
		W.Floor();
		Wall(W, 80.f, 100.f);
		W.Box(FVector(95.f, 0.f, 200.f), FVector(300.f, 400.f, 20.f));
		AAftermathCharacter* C = W.Survivor(FVector2D(0.f, 0.f));
		W.Tick(0.2f);
		UTEST_TRUE("no headroom rejected", !C->GetAftermathMovement()->QueryVault().bCanVault);
	}

	// Crouched can't vault.
	{
		FWorld W;
		UTEST_TRUE("world", W.Init(*this));
		W.Floor();
		Wall(W, 80.f, 60.f);
		AAftermathCharacter* C = W.Survivor(FVector2D(0.f, 0.f));
		C->ToggleCrouchStance();
		W.Tick(0.3f);
		UTEST_TRUE("crouched rejected", !C->GetAftermathMovement()->QueryVault().bCanVault);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAftermathVaultExecuteTest, "Aftermath.Movement.VaultExecutes", AftermathTest::Flags)
bool FAftermathVaultExecuteTest::RunTest(const FString& Parameters)
{
	FWorld W;
	UTEST_TRUE("world", W.Init(*this));
	W.Floor();
	Wall(W, 80.f, 100.f);
	AAftermathCharacter* C = W.Survivor(FVector2D(0.f, 0.f));
	UTEST_NOT_NULL("character", C);
	UAftermathMovementComponent* M = C->GetAftermathMovement();
	W.Tick(0.2f);

	C->JumpOrVault();
	W.Tick(1.f / 60.f);
	UTEST_TRUE("vault started", M->IsVaulting());

	float MaxBottom = 0.f;
	for (int32 i = 0; i < 120 && M->IsVaulting(); ++i)
	{
		W.Tick(1.f / 60.f);
		MaxBottom = FMath::Max(MaxBottom, C->GetActorLocation().Z - C->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	}
	W.Tick(0.5f);

	UTEST_TRUE("vault finished", !M->IsVaulting());
	UTEST_TRUE("cleared the wall top", MaxBottom >= 100.f);
	UTEST_TRUE("landed past the wall", C->GetActorLocation().X > 80.f + 30.f + 35.f);
	UTEST_TRUE("back on the ground", M->IsMovingOnGround());
	UTEST_TRUE("feet on floor", FMath::Abs(C->GetActorLocation().Z - 90.f) < 5.f);

	// Space on open ground is a normal jump, not a vault.
	C->JumpOrVault();
	W.Tick(1.f / 60.f);
	UTEST_TRUE("open ground: no vault", !M->IsVaulting());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAftermathVaultTooHighTest, "Aftermath.Movement.VaultTooHighDoesNothing", AftermathTest::Flags)
bool FAftermathVaultTooHighTest::RunTest(const FString& Parameters)
{
	FWorld W;
	UTEST_TRUE("world", W.Init(*this));
	W.Floor();
	Wall(W, 80.f, 180.f);
	AAftermathCharacter* C = W.Survivor(FVector2D(0.f, 0.f));
	UAftermathMovementComponent* M = C->GetAftermathMovement();
	W.Tick(0.2f);

	// Force the request even though the client check would refuse: simulates a cheating client.
	M->RequestVault();
	W.Tick(1.0f);
	UTEST_TRUE("no vault", !M->IsVaulting());
	UTEST_TRUE("stayed in front of the wall", C->GetActorLocation().X < 80.f);
	return true;
}

// ---------------------------------------------------------------------------
// World: interaction
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAftermathInteractServerTest, "Aftermath.Interaction.ServerValidatesReach", AftermathTest::Flags)
bool FAftermathInteractServerTest::RunTest(const FString& Parameters)
{
	FWorld W;
	UTEST_TRUE("world", W.Init(*this));
	W.Floor();
	AAftermathCharacter* C = W.Survivor(FVector2D(0.f, 0.f));
	UTEST_NOT_NULL("character", C);
	W.Tick(0.2f);

	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// Door within reach: toggles open then closed.
	AAftermathDoor* NearDoor = W.World->SpawnActor<AAftermathDoor>(FVector(150.f, -50.f, 0.f), FRotator(0.f, 90.f, 0.f), P);
	UTEST_TRUE("near door in reach", C->IsWithinInteractReach(NearDoor));
	UTEST_TRUE("open near door", C->ServerTryInteractWith(NearDoor));
	UTEST_TRUE("door open", NearDoor->IsOpen());
	W.Tick(1.0f);
	UTEST_NEARLY_EQUAL("leaf swung fully", FMath::Abs(NearDoor->GetCurrentYaw()), 95.f, 0.5f);
	{
		// Swung away from the user: the leaf's far end must be farther from the character than the hinge is.
		const FVector LeafEnd = NearDoor->GetActorTransform().TransformPosition(
			FRotator(0.f, NearDoor->GetCurrentYaw(), 0.f).RotateVector(FVector(100.f, 0.f, 0.f)));
		UTEST_TRUE("swings away from user",
			FVector::Dist2D(LeafEnd, C->GetActorLocation()) > FVector::Dist2D(NearDoor->GetActorLocation(), C->GetActorLocation()));
	}
	UTEST_TRUE("close near door", C->ServerTryInteractWith(NearDoor));
	UTEST_TRUE("door closed", !NearDoor->IsOpen());

	// Door far away: server refuses even though "the client asked".
	AAftermathDoor* FarDoor = W.World->SpawnActor<AAftermathDoor>(FVector(900.f, 0.f, 0.f), FRotator(0.f, 90.f, 0.f), P);
	// Both range rejections below log a warning; declare them so they don't fail the test.
	AddExpectedMessage(TEXT("InteractRejected reason=range"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 2, false);
	UTEST_TRUE("far door rejected", !C->ServerTryInteractWith(FarDoor));
	UTEST_TRUE("far door untouched", !FarDoor->IsOpen());

	// Pickup in reach: goes into inventory and is destroyed; a second request is ignored.
	// Storage comes from clothes, so wear a T-shirt first (its 2x2 pockets hold the bandage).
	UAftermathInventoryComponent* Inv = C->GetInventory();
	UTEST_NOT_NULL("inventory", Inv);
	Inv->AddItem(AftermathItems::MakeInstance(*UAftermathItemDatabase::Get()->Find(TEXT("TShirt"))));
	UTEST_EQUAL("wearing shirt", Inv->CountItems(), 1);

	AAftermathPickup* Near = W.World->SpawnActor<AAftermathPickup>(FVector(120.f, 0.f, 15.f), FRotator::ZeroRotator, P);
	Near->SetItemById(TEXT("Bandage"), 2);
	UTEST_TRUE("take near pickup", C->ServerTryInteractWith(Near));
	UTEST_TRUE("second take ignored", !C->ServerTryInteractWith(Near));
	UTEST_EQUAL("bandages carried", Inv->CountOf(TEXT("Bandage")), 2);
	UTEST_TRUE("pickup destroyed", !IsValid(Near) || Near->IsActorBeingDestroyed());

	// Pickup out of reach.
	AAftermathPickup* Far = W.World->SpawnActor<AAftermathPickup>(FVector(700.f, 0.f, 15.f), FRotator::ZeroRotator, P);
	UTEST_TRUE("far pickup rejected", !C->ServerTryInteractWith(Far));
	UTEST_EQUAL("still two bandages", Inv->CountOf(TEXT("Bandage")), 2);
	UTEST_TRUE("far pickup still there", IsValid(Far) && !Far->IsActorBeingDestroyed());

	// Non-interactable and null targets are refused.
	UTEST_TRUE("null refused", !C->ServerTryInteractWith(nullptr));
	AStaticMeshActor* Rock = W.Box(FVector(100.f, 100.f, 50.f), FVector(50.f));
	UTEST_TRUE("rock refused", !C->ServerTryInteractWith(Rock));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
