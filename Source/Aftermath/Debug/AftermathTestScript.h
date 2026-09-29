#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AftermathTestScript.generated.h"

class AAftermathCharacter;

/**
 * Dev-only scripted driver for multi-process network tests (Tools/smoke_stage1.sh).
 *
 * A client started with  -AftermathScript=<name>  runs a fixed sequence of timed steps on its own
 * pawn through the SAME input functions a player uses (so everything goes through prediction,
 * saved moves and server RPCs). Every step and every observation of the other player is logged as
 *   [AftermathTest] role=<name> ...
 * so the calling script can assert on the logs. Compiled out of Shipping builds.
 */
UCLASS(ClassGroup = (Aftermath), meta = (BlueprintSpawnableComponent))
class AFTERMATH_API UAftermathTestScript : public UActorComponent
{
	GENERATED_BODY()

public:
	UAftermathTestScript();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Reads -AftermathScript= from the command line. Empty when not running a script. */
	static FString GetRequestedScript();

protected:
	virtual void BeginPlay() override;

private:
	struct FStep
	{
		float AtSeconds;
		FString Name;
		TFunction<void(AAftermathCharacter&)> Action;
	};

	void BuildScript();
	void SortSteps();
	void LogObservations(AAftermathCharacter& Self);
	AAftermathCharacter* FindOtherSurvivor(const AAftermathCharacter& Self) const;
	void FaceTowards(AAftermathCharacter& Self, const FVector& Target);

	FString ScriptName;
	TArray<FStep> Steps;
	int32 NextStep = 0;
	float Elapsed = 0.f;
	float ObserveTimer = 0.f;
	bool bStarted = false;
	bool bReportedDeath = false;
	/** Forward input held until this time (script-relative seconds). */
	float HoldForwardUntil = -1.f;
	/** Press jump/vault as soon as QueryVault succeeds. */
	bool bVaultWhenPossible = false;
};
