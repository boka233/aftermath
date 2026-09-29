#include "Survival/AftermathSurvivalComponent.h"

#include "Aftermath.h"
#include "Combat/AftermathNoise.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Items/AftermathInventoryComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Net/UnrealNetwork.h"
#include "Player/AftermathCharacter.h"

UAftermathSurvivalComponent::UAftermathSurvivalComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// Survival changes slowly; 10 Hz is plenty and keeps 60+ players cheap on the server.
	PrimaryComponentTick.TickInterval = 0.1f;
	SetIsReplicatedByDefault(true);
	Rng.Initialize(FPlatformTime::Cycles());
}

void UAftermathSurvivalComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	// Only you know how hungry you are. Death is visible to everyone through the character (ragdoll later).
	DOREPLIFETIME_CONDITION(UAftermathSurvivalComponent, Vitals, COND_OwnerOnly);
}

void UAftermathSurvivalComponent::BeginPlay()
{
	Super::BeginPlay();
	// Only the server simulates; clients just receive Vitals.
	SetComponentTickEnabled(GetOwner() && GetOwner()->HasAuthority());
}

void UAftermathSurvivalComponent::OnRep_Vitals()
{
}

bool UAftermathSurvivalComponent::Roll(float Chance)
{
	if (bForceRolls)
	{
		return bForcedRollResult;
	}
	return Chance > 0.f && Rng.FRand() < Chance;
}

void UAftermathSurvivalComponent::ResetForSpawn()
{
	Vitals = FAftermathVitals();
	Vitals.Health = Tuning.MaxHealth;
	Vitals.Blood = Tuning.MaxBlood;
	// Fresh spawns start a bit hungry and thirsty, like DayZ.
	Vitals.Energy = Tuning.MaxEnergy * 0.6f;
	Vitals.Water = Tuning.MaxWater * 0.6f;
	Vitals.Stamina = Tuning.MaxStamina;
	SickTimeLeft = 0.f;
	PoisonTimeLeft = 0.f;
}

void UAftermathSurvivalComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	Simulate(DeltaTime);
}

void UAftermathSurvivalComponent::UpdateCarryWeight()
{
	if (const AAftermathCharacter* C = Cast<AAftermathCharacter>(GetOwner()))
	{
		if (const UAftermathInventoryComponent* Inv = C->GetInventory())
		{
			Vitals.CarryKg = Inv->TotalWeightGrams() / 1000.f;
		}
	}
}

void UAftermathSurvivalComponent::Simulate(float Dt)
{
	if (Vitals.bDead || Dt <= 0.f)
	{
		return;
	}
	const FAftermathSurvivalTuning& T = Tuning;
	FAftermathVitals& V = Vitals;

	UpdateCarryWeight();

	// ---- Activity ----
	float Speed2D = 0.f;
	bool bSprinting = false;
	const ACharacter* Char = Cast<ACharacter>(GetOwner());
	if (Char && Char->GetCharacterMovement())
	{
		Speed2D = Char->GetCharacterMovement()->Velocity.Size2D();
		bSprinting = Speed2D > 520.f;
	}
	const float Activity = AftermathSurvival::ActivityFromSpeed(T, Speed2D);

	// Footsteps: one noise report every 0.5 s while moving (cheap, and plenty for AI hearing).
	FootstepTimer -= Dt;
	if (FootstepTimer <= 0.f)
	{
		FootstepTimer = 0.5f;
		const AAftermathCharacter* Survivor = Cast<AAftermathCharacter>(GetOwner());
		const bool bLow = Survivor && Survivor->GetStance() != EAftermathStance::Standing;
		const float Radius = AftermathNoiseMath::FootstepRadius(Speed2D, bLow);
		if (Radius > 0.f)
		{
			if (UAftermathNoiseSubsystem* Noise = GetWorld() ? GetWorld()->GetSubsystem<UAftermathNoiseSubsystem>() : nullptr)
			{
				Noise->Report(EAftermathNoiseKind::Footstep, GetOwner()->GetActorLocation(), Radius, GetOwner());
			}
		}
	}

	// ---- Metabolism ----
	V.Energy = FMath::Max(0.f, V.Energy - T.EnergyDrain * Activity * Dt);
	V.Water = FMath::Max(0.f, V.Water - T.WaterDrain * Activity * Dt);
	if (V.Energy <= 0.f)
	{
		V.Health -= T.StarveHealthLoss * Dt;
	}
	if (V.Water <= 0.f)
	{
		V.Health -= T.DehydrateHealthLoss * Dt;
	}

	// ---- Stamina (carry weight shrinks the pool) ----
	const float WeightFactor = AftermathSurvival::WeightSpeedFactor(T, V.CarryKg);
	const float StaminaCap = T.MaxStamina * FMath::Lerp(0.4f, 1.f, (WeightFactor - 0.6f) / 0.4f);
	if (bSprinting)
	{
		V.Stamina -= T.SprintStaminaDrain * Dt;
	}
	else
	{
		V.Stamina += T.StaminaRegen * Dt * (Speed2D < 50.f ? 1.5f : 1.f);
	}
	V.Stamina = FMath::Clamp(V.Stamina, 0.f, StaminaCap);
	if (V.Stamina <= 1.f)
	{
		V.bExhausted = true;
	}
	else if (V.bExhausted && V.Stamina >= T.SprintMinStamina)
	{
		V.bExhausted = false;
	}

	// ---- Bleeding ----
	if (V.Cuts > 0)
	{
		V.Blood -= T.BloodLossPerCut * V.Cuts * Dt;
	}
	if (V.Blood < T.BloodLowThreshold)
	{
		const float Severity = 1.f - (V.Blood - T.BloodCriticalThreshold * 0.5f) / (T.BloodLowThreshold - T.BloodCriticalThreshold * 0.5f);
		V.Health -= T.LowBloodHealthLoss * FMath::Clamp(Severity, 0.2f, 2.f) * Dt;
	}

	// ---- Temperature ----
	float Insulation = 0.f;
	if (const AAftermathCharacter* C = Cast<AAftermathCharacter>(GetOwner()))
	{
		if (const UAftermathInventoryComponent* Inv = C->GetInventory())
		{
			Insulation = Inv->GetInsulation();
		}
	}
	V.Heat = FMath::Clamp(V.Heat + AftermathSurvival::HeatDelta(T, Env, Insulation, Activity, V.Heat) * Dt, 0.f, 1.f);
	if (V.Heat < T.FreezingThreshold)
	{
		V.Health -= T.FreezingHealthLoss * Dt;
	}
	if (V.Heat < T.ColdThreshold && !V.bSick && Roll(T.CatchColdChance * Dt))
	{
		GetSick();
	}

	// ---- Sickness: a cold makes you cough (noise for infected in stage 4), drains a little ----
	if (V.bSick)
	{
		SickTimeLeft -= Dt;
		V.Energy = FMath::Max(0.f, V.Energy - 0.1f * Dt);
		CoughTimer -= Dt;
		if (CoughTimer <= 0.f)
		{
			CoughTimer = T.CoughInterval;
			UE_LOG(LogAftermath, Verbose, TEXT("[Aftermath] Cough by=%s"), *GetNameSafe(GetOwner()));
			// A cough gives your position away (infected in stage 4, players nearby).
			if (UAftermathNoiseSubsystem* Noise = GetWorld() ? GetWorld()->GetSubsystem<UAftermathNoiseSubsystem>() : nullptr)
			{
				Noise->Report(EAftermathNoiseKind::Cough, GetOwner()->GetActorLocation(), 1500.f, GetOwner());
			}
		}
		if (SickTimeLeft <= 0.f)
		{
			V.bSick = false; // shook it off
		}
	}
	if (V.bPoisoned)
	{
		PoisonTimeLeft -= Dt;
		V.Water = FMath::Max(0.f, V.Water - T.PoisonWaterLoss * Dt);
		V.Energy = FMath::Max(0.f, V.Energy - T.PoisonEnergyLoss * Dt);
		if (PoisonTimeLeft <= 0.f)
		{
			V.bPoisoned = false;
		}
	}

	// ---- Regeneration ----
	const bool bWellFed = V.Energy > T.MaxEnergy * T.WellFedThreshold && V.Water > T.MaxWater * T.WellFedThreshold;
	if (bWellFed && V.Cuts == 0 && V.Heat >= T.ColdThreshold && !V.bPoisoned)
	{
		V.Blood = FMath::Min(T.MaxBlood, V.Blood + T.BloodRegen * Dt);
		if (V.Blood > T.BloodLowThreshold)
		{
			V.Health = FMath::Min(T.MaxHealth, V.Health + T.HealthRegen * Dt);
		}
	}

	V.Health = FMath::Min(V.Health, T.MaxHealth);
	if (V.Health <= 0.f || V.Blood <= 0.f)
	{
		Die(V.Blood <= 0.f ? TEXT("blood") : (V.Water <= 0.f ? TEXT("thirst") : (V.Energy <= 0.f ? TEXT("hunger") : TEXT("health"))));
	}

	// Replication: Vitals is a struct, so any change sends it. Throttle forced updates to ~2 Hz.
	NetAccumulator += Dt;
	if (NetAccumulator >= 0.5f)
	{
		NetAccumulator = 0.f;
		if (AActor* Owner = GetOwner())
		{
			Owner->ForceNetUpdate();
		}
	}
}

bool UAftermathSurvivalComponent::CanSprint() const
{
	// Broken legs never sprint, splinted or not (the splint only restores a slow jog).
	if (Vitals.bDead || Vitals.bBrokenLegs)
	{
		return false;
	}
	if (Vitals.CarryKg >= Tuning.WeightMaxKg)
	{
		return false;
	}
	return !Vitals.bExhausted && Vitals.Stamina > 1.f;
}

float UAftermathSurvivalComponent::GetSpeedMultiplier() const
{
	float M = AftermathSurvival::WeightSpeedFactor(Tuning, Vitals.CarryKg);
	if (Vitals.bBrokenLegs)
	{
		M *= Vitals.bSplinted ? 0.6f : 0.35f;
	}
	if (Vitals.Blood < Tuning.BloodCriticalThreshold)
	{
		M *= 0.7f;
	}
	return M;
}

void UAftermathSurvivalComponent::SpendStamina(float Amount)
{
	Vitals.Stamina = FMath::Max(0.f, Vitals.Stamina - FMath::Max(0.f, Amount));
	if (Vitals.Stamina <= 1.f)
	{
		Vitals.bExhausted = true;
	}
}

void UAftermathSurvivalComponent::AddCut(int32 Count)
{
	Vitals.Cuts = FMath::Clamp(Vitals.Cuts + Count, 0, 8);
}

void UAftermathSurvivalComponent::ApplyDamage(float HealthDamage, float BloodDamage)
{
	if (Vitals.bDead)
	{
		return;
	}
#if !UE_BUILD_SHIPPING
	// Load tests keep one player alive inside a horde (server started with both flags). Never in Shipping.
	static const bool bGodMode = FParse::Param(FCommandLine::Get(), TEXT("AftermathAllowDebug"))
		&& FParse::Param(FCommandLine::Get(), TEXT("AftermathGodMode"));
	if (bGodMode)
	{
		return;
	}
#endif
	Vitals.Health -= FMath::Max(0.f, HealthDamage);
	Vitals.Blood -= FMath::Max(0.f, BloodDamage);
	if (Vitals.Health <= 0.f || Vitals.Blood <= 0.f)
	{
		Die(TEXT("damage"));
	}
}

void UAftermathSurvivalComponent::BreakLegs()
{
	Vitals.bBrokenLegs = true;
	Vitals.bSplinted = false;
}

void UAftermathSurvivalComponent::GetSick()
{
	Vitals.bSick = true;
	SickTimeLeft = Tuning.SicknessDuration;
	CoughTimer = 2.f;
}

void UAftermathSurvivalComponent::GetPoisoned()
{
	Vitals.bPoisoned = true;
	PoisonTimeLeft = Tuning.PoisonDuration;
}

void UAftermathSurvivalComponent::OnLanded(float LandingSpeed)
{
	if (Vitals.bDead)
	{
		return;
	}
	const float Damage = AftermathSurvival::FallDamage(Tuning, LandingSpeed);
	if (Damage <= 0.f)
	{
		return;
	}
	if (LandingSpeed >= Tuning.FallBreakLegSpeed)
	{
		BreakLegs();
	}
	if (LandingSpeed >= Tuning.FallCutSpeed)
	{
		AddCut(1);
	}
	UE_LOG(LogAftermath, Display, TEXT("[Aftermath] FallDamage by=%s speed=%.0f damage=%.0f legs=%d"),
		*GetNameSafe(GetOwner()), LandingSpeed, Damage, Vitals.bBrokenLegs ? 1 : 0);
	ApplyDamage(Damage);
}

bool UAftermathSurvivalComponent::Bandage(float InfectionChance)
{
	if (Vitals.Cuts <= 0)
	{
		return false;
	}
	--Vitals.Cuts;
	if (Roll(InfectionChance) && !Vitals.bSick)
	{
		GetSick();
	}
	return true;
}

bool UAftermathSurvivalComponent::Splint()
{
	if (!Vitals.bBrokenLegs || Vitals.bSplinted)
	{
		return false;
	}
	Vitals.bSplinted = true;
	return true;
}

bool UAftermathSurvivalComponent::CureSickness()
{
	if (!Vitals.bSick)
	{
		return false;
	}
	// Medicine speeds recovery a lot rather than instantly curing (one dose = most of it).
	SickTimeLeft -= Tuning.SicknessDuration * 0.5f;
	if (SickTimeLeft <= 0.f)
	{
		Vitals.bSick = false;
	}
	return true;
}

bool UAftermathSurvivalComponent::CurePoisoning()
{
	if (!Vitals.bPoisoned)
	{
		return false;
	}
	Vitals.bPoisoned = false;
	PoisonTimeLeft = 0.f;
	return true;
}

void UAftermathSurvivalComponent::Eat(float Energy, float Water, float PoisonChance)
{
	if (Vitals.bDead)
	{
		return;
	}
	Vitals.Energy = FMath::Clamp(Vitals.Energy + Energy, 0.f, Tuning.MaxEnergy);
	Vitals.Water = FMath::Clamp(Vitals.Water + Water, 0.f, Tuning.MaxWater);
	if (Roll(PoisonChance))
	{
		GetPoisoned();
	}
}

void UAftermathSurvivalComponent::Die(const TCHAR* Cause)
{
	if (Vitals.bDead)
	{
		return;
	}
	Vitals.bDead = true;
	Vitals.Health = 0.f;
	Vitals.Cuts = 0;
	UE_LOG(LogAftermath, Display, TEXT("[Aftermath] PlayerDied who=%s cause=%s"), *GetNameSafe(GetOwner()), Cause);
	if (AActor* Owner = GetOwner())
	{
		Owner->ForceNetUpdate();
	}
	OnDied.Broadcast(GetOwner());
}
