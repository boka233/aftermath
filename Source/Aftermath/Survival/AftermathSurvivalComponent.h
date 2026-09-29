#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Survival/AftermathSurvivalMath.h"
#include "AftermathSurvivalComponent.generated.h"

/** Snapshot the owner (and only the owner) sees on the HUD. Quantized to keep bandwidth tiny. */
USTRUCT(BlueprintType)
struct FAftermathVitals
{
	GENERATED_BODY()

	UPROPERTY() float Health = 100.f;
	UPROPERTY() float Blood = 5000.f;
	UPROPERTY() float Energy = 2500.f;
	UPROPERTY() float Water = 1800.f;
	/** 0 frozen .. 0.5 comfortable .. 1 hot. */
	UPROPERTY() float Heat = 0.5f;
	UPROPERTY() float Stamina = 100.f;
	UPROPERTY() float CarryKg = 0.f;

	/** Ran out of stamina: no sprint until it recovers to SprintMinStamina (stops sprint flickering on/off). */
	UPROPERTY() bool bExhausted = false;

	UPROPERTY() int32 Cuts = 0;
	UPROPERTY() bool bBrokenLegs = false;
	UPROPERTY() bool bSplinted = false;
	UPROPERTY() bool bSick = false;
	UPROPERTY() bool bPoisoned = false;
	UPROPERTY() bool bDead = false;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FOnAftermathDied, AActor* /*Victim*/);

/**
 * Survivor stats and ailments. Simulated only on the server (never trusted from the client);
 * replicated to the owning client for the HUD. Movement speed caps are read by the movement component
 * on both sides from replicated values, so client prediction matches the server.
 */
UCLASS(ClassGroup = (Aftermath), meta = (BlueprintSpawnableComponent))
class AFTERMATH_API UAftermathSurvivalComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UAftermathSurvivalComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	FOnAftermathDied OnDied;

	const FAftermathVitals& GetVitals() const { return Vitals; }
	bool IsDead() const { return Vitals.bDead; }
	bool IsBleeding() const { return Vitals.Cuts > 0; }

	/** Can the owner start/continue sprinting? (legs, stamina, weight) Read on client and server. */
	bool CanSprint() const;
	/** Extra ground-speed multiplier from injuries and weight. Read on client and server. */
	float GetSpeedMultiplier() const;

	// ---- Server: things that happen to you ----
	void AddCut(int32 Count = 1);
	void ApplyDamage(float HealthDamage, float BloodDamage = 0.f);
	void BreakLegs();
	void GetSick();
	void GetPoisoned();
	void OnLanded(float LandingSpeed);

	// ---- Server: treatments (return false if there was nothing to treat) ----
	/** Server: spend stamina on an action (melee swing). Can go to 0; exhaustion rules apply as usual. */
	void SpendStamina(float Amount);

	bool Bandage(float InfectionChance = 0.f);
	bool Splint();
	bool CureSickness();
	bool CurePoisoning();
	void Eat(float Energy, float Water, float PoisonChance);

	/** Server: environment for the heat model (set by the world/weather later). */
	void SetEnvironment(const FAftermathEnvironment& InEnv) { Env = InEnv; }

	/** Server: fresh spawn state. */
	void ResetForSpawn();

	/** Server: advance the simulation by DeltaTime seconds (TickComponent calls this; tests call it directly). */
	void Simulate(float DeltaTime);

	/** Server: seed for deterministic tests (random rolls: colds, poisoning, infection). */
	void SetRandomSeed(int32 Seed) { Rng.Initialize(Seed); }

	/** Force the next roll results (tests). */
	void SetForceRolls(bool bInForce, bool bResult) { bForceRolls = bInForce; bForcedRollResult = bResult; }

	UPROPERTY(EditDefaultsOnly, Category = "Survival")
	FAftermathSurvivalTuning Tuning;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(ReplicatedUsing = OnRep_Vitals)
	FAftermathVitals Vitals;

	UFUNCTION()
	void OnRep_Vitals();

private:
	bool Roll(float Chance);
	void Die(const TCHAR* Cause);
	void UpdateCarryWeight();

	FAftermathEnvironment Env;
	FRandomStream Rng;
	float SickTimeLeft = 0.f;
	float PoisonTimeLeft = 0.f;
	float CoughTimer = 0.f;
	float FootstepTimer = 0.f;
	float NetAccumulator = 0.f;
	bool bForceRolls = false;
	bool bForcedRollResult = false;
};
