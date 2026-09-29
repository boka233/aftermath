// Stage 4 pure infected rules: vision, hearing, state machine, update rate, type armor.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Infected/AftermathInfectedMath.h"

namespace AftermathInfectedMathTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	using namespace AftermathInfectedMath;

	FVisionInput Seen(float Dist, EAftermathStance Stance = EAftermathStance::Standing, float Speed = 0.f, float Light = 1.f)
	{
		FVisionInput V;
		V.Eye = FVector(0, 0, 160.f);
		V.Forward = FVector::ForwardVector;
		V.Target = FVector(Dist, 0, 160.f);
		V.TargetStance = Stance;
		V.TargetSpeed = Speed;
		V.Light01 = Light;
		return V;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAftermathInfectedVisionTest, "Aftermath.Infected.Vision", AftermathInfectedMathTest::Flags)
bool FAftermathInfectedVisionTest::RunTest(const FString& Parameters)
{
	using namespace AftermathInfectedMathTest;

	const float Near = VisionRate(Seen(500.f));
	const float Far = VisionRate(Seen(2500.f));
	TestTrue("close standing target noticed fast (chase within ~0.5s)", Near * 0.5f >= ChaseThreshold);
	TestTrue("farther is slower", Far > 0.f && Far < Near);
	TestEqual("beyond range: nothing", VisionRate(Seen(5000.f)), 0.f);

	FVisionInput Blocked = Seen(500.f);
	Blocked.bLineOfSight = false;
	TestEqual("wall blocks sight", VisionRate(Blocked), 0.f);

	FVisionInput Behind = Seen(500.f);
	Behind.Target = FVector(-500.f, 0, 160.f);
	TestEqual("directly behind: unseen", VisionRate(Behind), 0.f);

	FVisionInput Side = Seen(500.f);
	Side.Target = FVector(-50.f, 500.f, 160.f); // ~96 deg: inside the 100 deg peripheral half-angle
	const float SideRate = VisionRate(Side);
	TestTrue("peripheral is weaker but not zero", SideRate > 0.f && SideRate < Near);

	FVisionInput Touch = Behind;
	Touch.Target = FVector(-100.f, 0, 160.f);
	TestTrue("someone right behind you is felt", VisionRate(Touch) > 0.f);

	// Stance, movement and light.
	TestTrue("crouching shrinks range", VisionRangeFor(EAftermathStance::Crouched, 0, 1) < VisionRangeFor(EAftermathStance::Standing, 0, 1));
	TestTrue("prone shrinks it more", VisionRangeFor(EAftermathStance::Prone, 0, 1) < VisionRangeFor(EAftermathStance::Crouched, 0, 1));
	TestTrue("sprinting widens it", VisionRangeFor(EAftermathStance::Standing, 650.f, 1) > VisionRangeFor(EAftermathStance::Standing, 0, 1));
	TestTrue("night shrinks it", VisionRangeFor(EAftermathStance::Standing, 0, 0.f) < VisionRangeFor(EAftermathStance::Standing, 0, 1.f) * 0.5f);
	TestEqual("prone still target at 15m is invisible", VisionRate(Seen(1500.f, EAftermathStance::Prone)), 0.f);
	TestEqual("standing at 20m at night is invisible", VisionRate(Seen(2000.f, EAftermathStance::Standing, 0.f, 0.f)), 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAftermathInfectedHearingTest, "Aftermath.Infected.Hearing", AftermathInfectedMathTest::Flags)
bool FAftermathInfectedHearingTest::RunTest(const FString& Parameters)
{
	using namespace AftermathInfectedMath;

	TestEqual("outside radius", Hearing(1500.f, 2000.f, false, 0.f), 0.f);
	TestTrue("inside radius", Hearing(1500.f, 500.f, false, 0.f) > 0.5f);
	// Walls: a sound at 60% of its radius is heard in the open but not through a wall (half radius).
	TestTrue("open", Hearing(1000.f, 600.f, false, 0.f) > 0.f);
	TestEqual("through a wall", Hearing(1000.f, 600.f, true, 0.f), 0.f);
	TestTrue("rain masks", Hearing(1000.f, 700.f, false, 1.f) == 0.f && Hearing(1000.f, 700.f, false, 0.f) > 0.f);

	// A gunshot 50m away (radius 200m) is loud enough to go straight to a chase-level awareness.
	TestTrue("near gunshot -> chase level", AwarenessFromSound(Hearing(20000.f, 5000.f, false, 0.f)) >= ChaseThreshold);
	// A footstep at the edge only makes them look.
	const float Faint = AwarenessFromSound(Hearing(1500.f, 1300.f, false, 0.f));
	TestTrue("faint footstep -> alert only", Faint >= 0.f && Faint < ChaseThreshold);
	TestEqual("silence", AwarenessFromSound(0.f), 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAftermathInfectedStateTest, "Aftermath.Infected.StateMachine", AftermathInfectedMathTest::Flags)
bool FAftermathInfectedStateTest::RunTest(const FString& Parameters)
{
	using namespace AftermathInfectedMath;
	using S = EAftermathInfectedState;

	FBrainInput In;
	TestEqual("idle stays idle", NextState(In), S::Idle);

	In.Awareness = 0.3f;
	In.bHasInterestPoint = true;
	TestEqual("heard something -> alert", NextState(In), S::Alert);

	FBrainInput Seen;
	Seen.bHasTarget = true;
	Seen.bTargetVisible = true;
	Seen.Awareness = 1.2f;
	Seen.DistToTarget = 1500.f;
	TestEqual("idle sees player -> chase", NextState(Seen), S::Chase);
	Seen.State = S::Alert;
	TestEqual("alert sees player -> chase", NextState(Seen), S::Chase);

	FBrainInput Called;
	Called.bCalledByOthers = true;
	TestEqual("roar from others -> alert", NextState(Called), S::Alert);

	FBrainInput Chase;
	Chase.State = S::Chase;
	Chase.bHasTarget = true;
	Chase.bTargetVisible = true;
	Chase.DistToTarget = 150.f;
	TestEqual("in reach -> attack", NextState(Chase), S::Attack);
	Chase.DistToTarget = 800.f;
	TestEqual("still far -> chase", NextState(Chase), S::Chase);
	Chase.bTargetVisible = false;
	Chase.SinceTargetSeen = 2.f;
	TestEqual("briefly lost -> keep chasing", NextState(Chase), S::Chase);
	Chase.SinceTargetSeen = 6.f;
	TestEqual("lost for a while -> search", NextState(Chase), S::Search);
	Chase.bHasTarget = false;
	TestEqual("target dead/gone -> search", NextState(Chase), S::Search);

	FBrainInput Attack;
	Attack.State = S::Attack;
	Attack.bHasTarget = true;
	Attack.DistToTarget = 600.f;
	Attack.TimeInState = 0.2f;
	TestEqual("mid-swing stays attacking", NextState(Attack), S::Attack);
	Attack.TimeInState = AttackWindup + AttackRecovery + 0.05f;
	TestEqual("target stepped back -> chase", NextState(Attack), S::Chase);
	Attack.DistToTarget = 120.f;
	TestEqual("still in reach -> swing again", NextState(Attack), S::Attack);

	FBrainInput Search;
	Search.State = S::Search;
	Search.TimeInState = 3.f;
	TestEqual("searching", NextState(Search), S::Search);
	Search.TimeInState = SearchSeconds + 1.f;
	TestEqual("gave up -> return", NextState(Search), S::Return);

	FBrainInput Ret;
	Ret.State = S::Return;
	Ret.bAtHome = false;
	TestEqual("walking home", NextState(Ret), S::Return);
	Ret.bAtHome = true;
	TestEqual("home -> idle", NextState(Ret), S::Idle);

	FBrainInput Dead;
	Dead.State = S::Dead;
	Dead.bHasTarget = Dead.bTargetVisible = true;
	Dead.Awareness = 2.f;
	TestEqual("dead stays dead", NextState(Dead), S::Dead);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAftermathInfectedLodTest, "Aftermath.Infected.UpdateRateAndTypes", AftermathInfectedMathTest::Flags)
bool FAftermathInfectedLodTest::RunTest(const FString& Parameters)
{
	using namespace AftermathInfectedMath;
	using S = EAftermathInfectedState;

	TestEqual("near: 10 Hz", ThinkInterval(1000.f, S::Idle), 0.1f);
	TestTrue("mid: slower", ThinkInterval(5000.f, S::Idle) > 0.1f);
	TestTrue("far: slowest", ThinkInterval(30000.f, S::Idle) >= ThinkInterval(10000.f, S::Idle));
	TestEqual("chasing always fast", ThinkInterval(30000.f, S::Chase), 0.1f);
	TestTrue("far idle sleeps", ShouldSleep(25000.f, S::Idle));
	TestFalse("far chasing doesn't", ShouldSleep(25000.f, S::Chase));
	TestFalse("near idle doesn't", ShouldSleep(5000.f, S::Idle));

	const FAftermathInfectedTypeDef& Civ = GetType(EAftermathInfectedType::Civilian);
	const FAftermathInfectedTypeDef& Pol = GetType(EAftermathInfectedType::Police);
	const FAftermathInfectedTypeDef& Sol = GetType(EAftermathInfectedType::Soldier);
	TestTrue("tougher types", Civ.Health < Pol.Health && Pol.Health < Sol.Health);
	TestEqual("civilian: no armor", DamageAfterArmor(Civ, false, true, 40.f), 40.f);
	TestTrue("police vest", DamageAfterArmor(Pol, false, true, 40.f) < 40.f);
	TestEqual("police bare head", DamageAfterArmor(Pol, true, false, 40.f), 40.f);
	TestTrue("soldier helmet", DamageAfterArmor(Sol, true, false, 40.f) < 40.f);
	TestEqual("limbs unarmored", DamageAfterArmor(Sol, false, false, 40.f), 40.f);
	return true;
}

#endif
