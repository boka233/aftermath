#pragma once

#include "CoreMinimal.h"
#include "AftermathSurvivalMath.generated.h"

/** All tunables in one place (units: per second unless stated). Numbers are loosely DayZ-shaped. */
USTRUCT(BlueprintType)
struct FAftermathSurvivalTuning
{
	GENERATED_BODY()

	// ---- Maxima ----
	UPROPERTY(EditAnywhere) float MaxHealth = 100.f;
	UPROPERTY(EditAnywhere) float MaxBlood = 5000.f;     // ml
	UPROPERTY(EditAnywhere) float MaxEnergy = 3500.f;    // kcal-ish
	UPROPERTY(EditAnywhere) float MaxWater = 2500.f;     // ml
	UPROPERTY(EditAnywhere) float MaxStamina = 100.f;

	// ---- Metabolism (drain per second, idle). Multiplied by activity. ----
	UPROPERTY(EditAnywhere) float EnergyDrain = 0.15f;
	UPROPERTY(EditAnywhere) float WaterDrain = 0.2f;
	/** Activity multipliers for metabolism. */
	UPROPERTY(EditAnywhere) float JogMetabolism = 2.f;
	UPROPERTY(EditAnywhere) float SprintMetabolism = 4.f;

	// ---- Starvation / dehydration ----
	UPROPERTY(EditAnywhere) float StarveHealthLoss = 0.25f;
	UPROPERTY(EditAnywhere) float DehydrateHealthLoss = 0.35f;

	// ---- Regeneration (only when fed, watered, not bleeding, not cold) ----
	UPROPERTY(EditAnywhere) float WellFedThreshold = 0.5f; // fraction of max
	UPROPERTY(EditAnywhere) float BloodRegen = 1.5f;
	UPROPERTY(EditAnywhere) float HealthRegen = 0.1f;

	// ---- Bleeding ----
	UPROPERTY(EditAnywhere) float BloodLossPerCut = 8.f;
	/** Below this blood the character starts losing health and blacks out at the lowest band. */
	UPROPERTY(EditAnywhere) float BloodLowThreshold = 3500.f;
	UPROPERTY(EditAnywhere) float BloodCriticalThreshold = 2500.f;
	UPROPERTY(EditAnywhere) float LowBloodHealthLoss = 0.4f;

	// ---- Temperature (heat balance, 0 = frozen, 1 = hot, 0.5 comfortable) ----
	UPROPERTY(EditAnywhere) float HeatRate = 0.004f;
	UPROPERTY(EditAnywhere) float ColdThreshold = 0.3f;
	UPROPERTY(EditAnywhere) float FreezingThreshold = 0.15f;
	UPROPERTY(EditAnywhere) float FreezingHealthLoss = 0.3f;
	/** Chance per second of catching a cold while cold. */
	UPROPERTY(EditAnywhere) float CatchColdChance = 0.002f;

	// ---- Stamina ----
	UPROPERTY(EditAnywhere) float SprintStaminaDrain = 12.f;
	UPROPERTY(EditAnywhere) float StaminaRegen = 8.f;
	/** Stamina needed to start sprinting. */
	UPROPERTY(EditAnywhere) float SprintMinStamina = 15.f;

	// ---- Carry weight ----
	/** Above this, slower and less stamina; at Max you can't sprint. */
	UPROPERTY(EditAnywhere) float WeightComfortKg = 15.f;
	UPROPERTY(EditAnywhere) float WeightMaxKg = 40.f;

	// ---- Sickness ----
	UPROPERTY(EditAnywhere) float SicknessDuration = 240.f;   // seconds without medicine
	UPROPERTY(EditAnywhere) float PoisonDuration = 90.f;
	UPROPERTY(EditAnywhere) float PoisonWaterLoss = 3.f;
	UPROPERTY(EditAnywhere) float PoisonEnergyLoss = 2.f;
	UPROPERTY(EditAnywhere) float CoughInterval = 12.f;

	// ---- Falls ----
	/** Landing speed (cm/s) at which a fall starts to hurt, and at which it kills. */
	UPROPERTY(EditAnywhere) float FallDamageSpeed = 900.f;
	UPROPERTY(EditAnywhere) float FallLethalSpeed = 1800.f;
	/** Landing speed above which legs break. */
	UPROPERTY(EditAnywhere) float FallBreakLegSpeed = 1100.f;
	/** A cut on a hard landing. */
	UPROPERTY(EditAnywhere) float FallCutSpeed = 1300.f;
};

/** Environment inputs to the heat model. */
struct FAftermathEnvironment
{
	/** -1 very cold ... 0 mild ... +1 very hot. */
	float AmbientWarmth = -0.2f;
	/** Rain intensity 0..1. */
	float Rain = 0.f;
};

namespace AftermathSurvival
{
	/**
	 * Heat balance rate per second. Positive warms the character.
	 * Insulation (0..1, clothes) and activity pull toward warm; cold ambient and rain pull toward cold.
	 */
	AFTERMATH_API float HeatDelta(const FAftermathSurvivalTuning& T, const FAftermathEnvironment& Env, float Insulation, float Activity, float CurrentHeat);

	/** 0..1 speed multiplier from carried weight (1 at/below comfort, 0.6 at max). */
	AFTERMATH_API float WeightSpeedFactor(const FAftermathSurvivalTuning& T, float Kg);

	/** Fall damage (health points) for a landing speed. 0 below the threshold, >= MaxHealth at lethal. */
	AFTERMATH_API float FallDamage(const FAftermathSurvivalTuning& T, float LandingSpeed);

	/** Metabolism multiplier from movement speed (cm/s). */
	AFTERMATH_API float ActivityFromSpeed(const FAftermathSurvivalTuning& T, float Speed2D);
}
