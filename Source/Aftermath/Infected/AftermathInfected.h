#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Combat/AftermathCombatComponent.h"
#include "Infected/AftermathInfectedMath.h"
#include "AftermathInfected.generated.h"

class AAftermathCharacter;
class UAftermathInfectedManager;
class UMaterialInstanceDynamic;
class UStaticMeshComponent;
struct FAftermathNoiseEvent;

/**
 * Server-driven infected. No AIController: the server steers the character movement directly
 * (bRunPhysicsWithNoController) along navmesh paths, and clients only see replicated movement plus the
 * replicated state (for colour).
 *
 * Brain (server): perception (vision + hearing) feeds AftermathInfectedMath::NextState at a rate that
 * UAftermathInfectedManager picks from the distance to the nearest player. Path following and the attack
 * swing run every frame while awake. Infected never open doors: they never call the interaction path,
 * and a closed door leaf blocks both movement and the navmesh.
 */
UCLASS()
class AFTERMATH_API AAftermathInfected : public ACharacter
{
	GENERATED_BODY()

public:
	AAftermathInfected(const FObjectInitializer& ObjectInitializer);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	// ---- Queries (server and clients) ----
	bool IsDead() const { return bDead; }
	EAftermathInfectedState GetState() const { return static_cast<EAftermathInfectedState>(RepState); }
	EAftermathInfectedType GetInfectedType() const { return static_cast<EAftermathInfectedType>(TypeIndex); }
	void SetInfectedType(EAftermathInfectedType Type);

	// ---- Server ----
	/** Perception + state machine. Dt = time since the last think. */
	void ServerThink(float Dt, const TArray<AAftermathCharacter*>& Players);
	void HearNoise(const FAftermathNoiseEvent& Event);
	/** Another infected roared (optionally about a target it saw). False if too busy to care. */
	bool HearRoar(const FVector& From, AAftermathCharacter* RoarTarget);
	/** A bullet or a swing landed. Returns what the attacker's hit marker should show. */
	FAftermathHitNotify ServerApplyHit(EAftermathHitZone Zone, float Damage, const FVector& From, const FVector& Impact, AActor* Attacker);
	/** Far from every player and idle: stop ticking (the manager decides). */
	void SetSleeping(bool bSleep);

	bool IsSleeping() const { return bSleeping; }
	float GetHealth() const { return Health; }
	float GetAwareness() const { return Awareness; }
	AAftermathCharacter* GetTarget() const { return Target.Get(); }
	FVector GetHome() const { return Home; }
	bool HasInterestPoint() const { return bHasInterestPoint; }
	FVector GetInterestPoint() const { return InterestPoint; }

	/** Idle infected wander around their spawn point. Tests turn this off for deterministic facing. */
	UPROPERTY(EditAnywhere, Category = "Infected")
	bool bAllowWander = true;

	// Manager bookkeeping (server).
	float ThinkAccum = 0.f;
	bool bThinkNow = false;
	int32 ThinkCount = 0;

protected:
	UPROPERTY(EditAnywhere, ReplicatedUsing = OnRep_Look, Category = "Infected")
	uint8 TypeIndex = 0;

	UPROPERTY(ReplicatedUsing = OnRep_Look)
	uint8 RepState = 0;

	UPROPERTY(ReplicatedUsing = OnRep_Dead)
	bool bDead = false;

	UFUNCTION()
	void OnRep_Look();

	UFUNCTION()
	void OnRep_Dead();

	UPROPERTY(VisibleAnywhere, Category = "Infected")
	TObjectPtr<UStaticMeshComponent> PlaceholderBody;

	/** Small block on the face so you can tell which way it looks. */
	UPROPERTY(VisibleAnywhere, Category = "Infected")
	TObjectPtr<UStaticMeshComponent> PlaceholderFace;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BodyMaterial;

private:
	const FAftermathInfectedTypeDef& Def() const { return AftermathInfectedMath::GetType(GetInfectedType()); }
	UAftermathInfectedManager* GetManager() const;
	void SetState(EAftermathInfectedState NewState, const TCHAR* Reason);
	void Die(AActor* Killer, EAftermathHitZone Zone);
	void ApplyDeadVisuals();
	void UpdateLook();

	FVector EyeLocation() const;
	bool HasLineOfSight(const FVector& From, const FVector& To, const AActor* IgnoreOther) const;
	float VisionRateFor(const AAftermathCharacter& Who, float Light01) const;

	// Movement
	void MoveTo(const FVector& Goal, bool bRun);
	void StopMoving();
	void FollowPath(float DeltaSeconds);
	void PickWanderPoint(const FVector& Center, float Radius);
	bool HasArrived() const { return PathIndex >= PathPoints.Num(); }
	void FaceTowards(const FVector& Where, float DeltaSeconds);
	void UpdateAttack(float DeltaSeconds);

	// Server state
	TWeakObjectPtr<UAftermathInfectedManager> Manager;
	EAftermathInfectedState State = EAftermathInfectedState::Idle;
	float Health = 100.f;
	float Awareness = 0.f;
	float TimeInState = 0.f;
	float SinceTargetSeen = 0.f;
	TWeakObjectPtr<AAftermathCharacter> Target;
	bool bTargetVisible = false;
	bool bForceSeeTarget = false;   // just got hit by the target: we know exactly where it is
	bool bCalledByOthers = false;
	bool bHasInterestPoint = false;
	FVector InterestPoint = FVector::ZeroVector;
	FVector LastSeenLocation = FVector::ZeroVector;
	FVector Home = FVector::ZeroVector;
	FVector SearchCenter = FVector::ZeroVector;
	const TCHAR* LastStimulus = TEXT("none");
	double LastRoarTime = -1000.0;
	bool bSwingPending = false;
	float WanderWait = 0.f;
	bool bSleeping = false;

	TArray<FVector> PathPoints;
	int32 PathIndex = 0;
	FVector PathGoal = FVector(1e9);
	double LastPathTime = -1000.0;
	float StuckTime = 0.f;
};
