// Stage 3 combat world tests: two real characters, server-side fire/melee validation, hit zones,
// armor, walls, lag compensation, noise.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#include "Combat/AftermathCombatComponent.h"
#include "Combat/AftermathHitHistory.h"
#include "Combat/AftermathNoise.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Items/AftermathInventoryComponent.h"
#include "Items/AftermathItemDatabase.h"
#include "Player/AftermathCharacter.h"
#include "Survival/AftermathSurvivalComponent.h"

namespace AftermathCombatTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	FAftermathItemInstance Make(FName Id, int32 Count = 1)
	{
		const FAftermathItemDef* D = UAftermathItemDatabase::Get()->Find(Id);
		return D ? AftermathItems::MakeInstance(*D, Count) : FAftermathItemInstance();
	}

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
			Box(FVector(0, 0, -25.f), FVector(20000.f, 20000.f, 50.f));
			return World != nullptr;
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

		void Tick(float Seconds)
		{
			for (float T = 0.f; T < Seconds; T += 1.f / 30.f)
			{
				Wrapper.TickTestWorld(1.f / 30.f);
			}
		}
	};

	/** Give Who a gun with a loaded magazine in hands. */
	void Arm(AAftermathCharacter* Who, FName Gun, FName Mag, int32 Rounds)
	{
		UAftermathInventoryComponent* Inv = Who->GetInventory();
		FAftermathItemInstance G = Make(Gun);
		G.AttachedMag = Mag;
		G.AttachedMagRounds = Rounds;
		Inv->AddItem(G);
	}

	/** Aim: GetBaseAimRotation() on a controller-less pawn uses the actor rotation (+ RemoteViewPitch). */
	void Face(AAftermathCharacter* Who, const FVector& Target)
	{
		const FRotator R = (Target - Who->GetInteractEyeLocation()).Rotation();
		Who->SetActorRotation(FRotator(0.f, R.Yaw, 0.f));
		Who->SetRemoteViewPitch(R.Pitch);
	}

	FAftermathShotOutcome Shoot(AAftermathCharacter* Who, const FVector& AimAt, int32 Seed = 1, double Time = -1.0)
	{
		Face(Who, AimAt);
		const FVector Eye = Who->GetInteractEyeLocation();
		const double T = Time >= 0.0 ? Time : Who->GetWorld()->GetTimeSeconds();
		// Aimed fire (tight cone) keeps these tests deterministic in where the bullet goes.
		Who->GetCombat()->SetAiming(true);
		return Who->GetCombat()->ServerHandleFire(Eye, (AimAt - Eye).GetSafeNormal(), Seed, T);
	}
}

using namespace AftermathCombatTest;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAftermathShotZonesTest, "Aftermath.Combat.ShotHitsZonesAndKills", AftermathCombatTest::Flags)
bool FAftermathShotZonesTest::RunTest(const FString& Parameters)
{
	FWorld W;
	UTEST_TRUE("world", W.Init(*this));
	AAftermathCharacter* Shooter = W.Survivor(FVector(0, 0, 92.f));
	AAftermathCharacter* Target = W.Survivor(FVector(1500.f, 0, 92.f), 180.f);
	Arm(Shooter, TEXT("Rifle"), TEXT("Mag545"), 30);
	W.Tick(0.3f);
	UAftermathSurvivalComponent* TS = Target->GetSurvival();
	TS->SetForceRolls(true, false);
	const FVector T = Target->GetActorLocation();

	// Legs.
	FAftermathShotOutcome O = Shoot(Shooter, T + FVector(0, 0, -60.f));
	UTEST_EQUAL("fired", O.Result, EAftermathShotResult::Fired);
	UTEST_TRUE("hit the target", O.bHitCharacter && O.Victim.Get() == Target);
	UTEST_EQUAL("legs", O.Zone, EAftermathHitZone::Legs);
	const float AfterLegs = TS->GetVitals().Health;
	UTEST_TRUE("hurt", AfterLegs < 100.f && AfterLegs > 40.f);
	UTEST_TRUE("bled", TS->GetVitals().Blood < TS->Tuning.MaxBlood);
	UTEST_EQUAL("round spent", Shooter->GetCombat()->GetHeldRounds(), 29);

	// Chest.
	W.Tick(0.2f);
	O = Shoot(Shooter, T + FVector(0, 0, 40.f));
	UTEST_EQUAL("torso", O.Zone, EAftermathHitZone::Torso);
	UTEST_TRUE("torso hits harder than legs", (AfterLegs - TS->GetVitals().Health) > (100.f - AfterLegs));

	// Head: rifle headshot kills.
	W.Tick(0.2f);
	O = Shoot(Shooter, T + FVector(0, 0, 80.f));
	UTEST_EQUAL("head", O.Zone, EAftermathHitZone::Head);
	UTEST_TRUE("dead", TS->IsDead() && Target->IsDead());

	// Dead bodies don't absorb more bullets.
	W.Tick(0.2f);
	O = Shoot(Shooter, T + FVector(0, 0, 40.f));
	UTEST_TRUE("shoots through where the corpse stood", !O.bHitCharacter);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAftermathShotArmorTest, "Aftermath.Combat.ArmorReducesDamage", AftermathCombatTest::Flags)
bool FAftermathShotArmorTest::RunTest(const FString& Parameters)
{
	float NakedDamage = 0.f, ArmoredDamage = 0.f, HelmetHeadDamage = 0.f;
	float VestHealthAfter = 1.f;
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		FWorld W;
		UTEST_TRUE("world", W.Init(*this));
		AAftermathCharacter* Shooter = W.Survivor(FVector(0, 0, 92.f));
		AAftermathCharacter* Target = W.Survivor(FVector(1000.f, 0, 92.f), 180.f);
		Arm(Shooter, TEXT("Pistol"), TEXT("Mag9mm"), 15);
		if (Pass == 1)
		{
			Target->GetInventory()->AddItem(Make(TEXT("PlateCarrier")));
			Target->GetInventory()->AddItem(Make(TEXT("Helmet")));
		}
		W.Tick(0.3f);
		Target->GetSurvival()->SetForceRolls(true, false);
		Shoot(Shooter, Target->GetActorLocation() + FVector(0, 0, 40.f));
		const float Torso = 100.f - Target->GetSurvival()->GetVitals().Health;
		if (Pass == 0)
		{
			NakedDamage = Torso;
		}
		else
		{
			ArmoredDamage = Torso;
			VestHealthAfter = Target->GetInventory()->GetSlot(EAftermathWearSlot::Vest)->Item.Health;
			W.Tick(0.2f);
			const float Before = Target->GetSurvival()->GetVitals().Health;
			Shoot(Shooter, Target->GetActorLocation() + FVector(0, 0, 80.f));
			HelmetHeadDamage = Before - Target->GetSurvival()->GetVitals().Health;
		}
	}
	AddInfo(FString::Printf(TEXT("pistol torso: naked %.1f armored %.1f; head w/ helmet %.1f"), NakedDamage, ArmoredDamage, HelmetHeadDamage));
	UTEST_TRUE("vest stops about half", ArmoredDamage < NakedDamage * 0.6f && ArmoredDamage > 0.f);
	UTEST_TRUE("vest wears down", VestHealthAfter < 1.f);
	UTEST_TRUE("helmet saves a pistol headshot", HelmetHeadDamage < 100.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAftermathShotWallTest, "Aftermath.Combat.WallsStopBullets", AftermathCombatTest::Flags)
bool FAftermathShotWallTest::RunTest(const FString& Parameters)
{
	FWorld W;
	UTEST_TRUE("world", W.Init(*this));
	AAftermathCharacter* Shooter = W.Survivor(FVector(0, 0, 92.f));
	AAftermathCharacter* Target = W.Survivor(FVector(1500.f, 0, 92.f), 180.f);
	W.Box(FVector(750.f, 0.f, 150.f), FVector(30.f, 400.f, 300.f));
	Arm(Shooter, TEXT("Rifle"), TEXT("Mag545"), 30);
	W.Tick(0.3f);

	const FAftermathShotOutcome O = Shoot(Shooter, Target->GetActorLocation() + FVector(0, 0, 40.f));
	UTEST_TRUE("hit the wall", O.bHitWorld && !O.bHitCharacter);
	UTEST_NEARLY_EQUAL("stopped at the wall", (float)O.ImpactPoint.X, 735.f, 5.f);
	UTEST_EQUAL("target unhurt", Target->GetSurvival()->GetVitals().Health, 100.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAftermathShotValidationTest, "Aftermath.Combat.ServerRejectsBadShots", AftermathCombatTest::Flags)
bool FAftermathShotValidationTest::RunTest(const FString& Parameters)
{
	FWorld W;
	UTEST_TRUE("world", W.Init(*this));
	AAftermathCharacter* Shooter = W.Survivor(FVector(0, 0, 92.f));
	AAftermathCharacter* Target = W.Survivor(FVector(1000.f, 0, 92.f), 180.f);
	W.Tick(0.3f);
	UAftermathCombatComponent* Combat = Shooter->GetCombat();
	const FVector Aim = Target->GetActorLocation() + FVector(0, 0, 40.f);

	UTEST_EQUAL("no gun", Shoot(Shooter, Aim).Result, EAftermathShotResult::NoWeapon);
	Arm(Shooter, TEXT("Pistol"), TEXT("Mag9mm"), 2);

	// Origin far from the shooter: teleport-shot cheat.
	Face(Shooter, Aim);
	UTEST_EQUAL("bad origin", Combat->ServerHandleFire(FVector(900.f, 0.f, 150.f), FVector::ForwardVector, 1, W.World->GetTimeSeconds()).Result,
		EAftermathShotResult::BadOrigin);
	// Aim way off the view: shooting behind yourself.
	UTEST_EQUAL("bad aim", Combat->ServerHandleFire(Shooter->GetInteractEyeLocation(), -FVector::ForwardVector, 1, W.World->GetTimeSeconds()).Result,
		EAftermathShotResult::BadAim);
	UTEST_EQUAL("rejected shots cost nothing", Combat->GetHeldRounds(), 2);

	// Fire rate: pistol 450 rpm = 0.133 s. Three shots in the same instant: second one refused.
	UTEST_EQUAL("first", Shoot(Shooter, Aim, 1).Result, EAftermathShotResult::Fired);
	UTEST_EQUAL("too fast", Shoot(Shooter, Aim, 2).Result, EAftermathShotResult::TooFast);
	W.Tick(0.2f);
	UTEST_EQUAL("second after the interval", Shoot(Shooter, Aim, 3).Result, EAftermathShotResult::Fired);
	W.Tick(0.2f);
	UTEST_EQUAL("empty", Shoot(Shooter, Aim, 4).Result, EAftermathShotResult::NoAmmo);

	// Dead shooters can't shoot.
	Shooter->GetSurvival()->ApplyDamage(1000.f);
	UTEST_EQUAL("dead", Shoot(Shooter, Aim, 5).Result, EAftermathShotResult::Dead);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAftermathShotRewindTest, "Aftermath.Combat.LagCompensation", AftermathCombatTest::Flags)
bool FAftermathShotRewindTest::RunTest(const FString& Parameters)
{
	FWorld W;
	UTEST_TRUE("world", W.Init(*this));
	AAftermathCharacter* Shooter = W.Survivor(FVector(0, 0, 92.f));
	AAftermathCharacter* Target = W.Survivor(FVector(2000.f, -200.f, 92.f), 90.f);
	Arm(Shooter, TEXT("Rifle"), TEXT("Mag545"), 30);
	UAftermathHitHistory* History = W.World->GetSubsystem<UAftermathHitHistory>();
	UTEST_NOT_NULL("history subsystem on the server world", History);
	W.Tick(0.3f);

	// Target strafes sideways at 5 m/s for 0.3 s.
	const FVector Seen = Target->GetActorLocation();
	const double SeenTime = W.World->GetTimeSeconds();
	for (int32 i = 0; i < 9; ++i)
	{
		Target->SetActorLocation(Target->GetActorLocation() + FVector(0.f, 500.f / 30.f, 0.f));
		W.Wrapper.TickTestWorld(1.f / 30.f);
	}
	const FVector Now = Target->GetActorLocation();
	UTEST_TRUE("moved well past its width", FVector::Dist2D(Seen, Now) > 120.f);

	// The shooter's screen lags the server: it fires at where the target WAS 0.15 s ago.
	// Without rewind that spot is empty now (the target moved > 1 body width); with rewind it's a hit.
	const double ShotTime = W.World->GetTimeSeconds() - 0.15;
	FAftermathPoseSample Pose;
	UTEST_TRUE("pose at shot time", History->GetPoseAt(Target, ShotTime, Pose));
	UTEST_TRUE("target has moved away from that spot", FVector::Dist2D(Pose.Location, Target->GetActorLocation()) > 70.f);
	FAftermathShotOutcome O = Shoot(Shooter, Pose.Location + FVector(0, 0, 40.f), 2, ShotTime);
	UTEST_TRUE("hit where the shooter saw it", O.bHitCharacter && O.Victim.Get() == Target);
	(void)Seen;
	(void)SeenTime;

	// Same aim but claiming a time 2 s in the past: clamped to 250 ms, the target isn't there -> miss.
	W.Tick(0.3f);
	for (int32 i = 0; i < 15; ++i)
	{
		Target->SetActorLocation(Target->GetActorLocation() + FVector(0.f, 500.f / 30.f, 0.f));
		W.Wrapper.TickTestWorld(1.f / 30.f);
	}
	FAftermathPoseSample Old;
	History->GetPoseAt(Target, W.World->GetTimeSeconds() - 0.45, Old);
	O = Shoot(Shooter, Old.Location + FVector(0, 0, 40.f), 3, W.World->GetTimeSeconds() - 2.0);
	UTEST_TRUE("can't shoot into the distant past", !O.bHitCharacter);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAftermathMeleeTest, "Aftermath.Combat.MeleeAndNoise", AftermathCombatTest::Flags)
bool FAftermathMeleeTest::RunTest(const FString& Parameters)
{
	FWorld W;
	UTEST_TRUE("world", W.Init(*this));
	AAftermathCharacter* A = W.Survivor(FVector(0, 0, 92.f));
	AAftermathCharacter* B = W.Survivor(FVector(110.f, 0, 92.f), 180.f);
	W.Tick(0.3f);
	B->GetSurvival()->SetForceRolls(true, false);
	UAftermathNoiseSubsystem* Noise = W.World->GetSubsystem<UAftermathNoiseSubsystem>();
	UTEST_NOT_NULL("noise subsystem", Noise);
	const double T0 = W.World->GetTimeSeconds();

	// Fists.
	Face(A, B->GetActorLocation() + FVector(0, 0, 40.f));
	UTEST_TRUE("punch lands", A->GetCombat()->ServerHandleMelee(A->GetBaseAimRotation().Vector()));
	const float AfterPunch = B->GetSurvival()->GetVitals().Health;
	UTEST_TRUE("punch hurts a little", AfterPunch < 100.f && AfterPunch > 85.f);
	UTEST_TRUE("too soon", !A->GetCombat()->ServerHandleMelee(A->GetBaseAimRotation().Vector()));

	// Axe hits much harder and costs stamina.
	A->GetInventory()->AddItem(Make(TEXT("Axe")));
	W.Tick(1.2f);
	const float Stamina = A->GetSurvival()->GetVitals().Stamina;
	UTEST_TRUE("axe lands", A->GetCombat()->ServerHandleMelee(A->GetBaseAimRotation().Vector()));
	UTEST_TRUE("axe hurts more", (AfterPunch - B->GetSurvival()->GetVitals().Health) > (100.f - AfterPunch) * 2.f);
	UTEST_TRUE("swing costs stamina", A->GetSurvival()->GetVitals().Stamina < Stamina);

	// Out of reach.
	B->SetActorLocation(FVector(600.f, 0, 92.f));
	W.Tick(1.2f);
	UTEST_TRUE("too far", !A->GetCombat()->ServerHandleMelee(A->GetBaseAimRotation().Vector()));

	// Gunshots are loud; noise reaches a listener 100 m away but not 300 m away.
	Arm(B, TEXT("Rifle"), TEXT("Mag545"), 5);
	W.Tick(0.2f);
	Shoot(B, FVector(5000.f, 0.f, 100.f));
	UTEST_TRUE("heard at 100 m", Noise->CountHeardAt(B->GetActorLocation() + FVector(10000.f, 0, 0), T0) > 0);
	UTEST_EQUAL("not at 450 m", Noise->CountHeardAt(B->GetActorLocation() + FVector(45000.f, 0, 0), T0), 0);

	FAftermathNoiseEvent E;
	E.Radius = 1000.f;
	UTEST_NEARLY_EQUAL("half loud halfway", AftermathNoiseMath::Attenuate(E, FVector(500.f, 0, 0), 0.f), 0.5f, 0.001f);
	UTEST_EQUAL("rain masks", AftermathNoiseMath::Attenuate(E, FVector(700.f, 0, 0), 1.f), 0.f);
	UTEST_TRUE("sprinting is loud", AftermathNoiseMath::FootstepRadius(650.f, false) > AftermathNoiseMath::FootstepRadius(180.f, false));
	UTEST_TRUE("crouching is quiet", AftermathNoiseMath::FootstepRadius(200.f, true) < AftermathNoiseMath::FootstepRadius(200.f, false));
	UTEST_EQUAL("standing still is silent", AftermathNoiseMath::FootstepRadius(0.f, false), 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAftermathReloadTest, "Aftermath.Combat.ReloadTakesTime", AftermathCombatTest::Flags)
bool FAftermathReloadTest::RunTest(const FString& Parameters)
{
	FWorld W;
	UTEST_TRUE("world", W.Init(*this));
	AAftermathCharacter* C = W.Survivor(FVector(0, 0, 92.f));
	C->GetInventory()->AddItem(Make(TEXT("Backpack")));
	Arm(C, TEXT("Pistol"), TEXT("Mag9mm"), 1);
	FAftermathItemInstance Full = Make(TEXT("Mag9mm"));
	Full.Loaded = 15;
	C->GetInventory()->AddItem(Full);
	W.Tick(0.2f);
	UAftermathCombatComponent* Combat = C->GetCombat();

	UTEST_TRUE("start reload", Combat->ServerHandleReload());
	UTEST_TRUE("reloading", Combat->IsReloading());
	UTEST_EQUAL("can't shoot while reloading", Shoot(C, FVector(1000, 0, 100)).Result, EAftermathShotResult::Reloading);
	UTEST_TRUE("can't double-reload", !Combat->ServerHandleReload());
	W.Tick(1.8f);
	UTEST_TRUE("done", !Combat->IsReloading());
	UTEST_EQUAL("full mag in", Combat->GetHeldRounds(), 15);
	UTEST_TRUE("nothing better to load", !Combat->ServerHandleReload());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
