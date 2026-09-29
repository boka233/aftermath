#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Movement/AftermathMovementMath.h"
#include "AftermathCharacter.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UStaticMeshComponent;
class UAftermathMovementComponent;
class IAftermathInteractable;
class UAftermathInventoryComponent;
class UAftermathSurvivalComponent;
class UAftermathCombatComponent;

/**
 * Player survivor.
 * - Movement: UAftermathMovementComponent (client prediction + server authority).
 * - Interaction: client traces for a target, asks the server; the server re-validates range before acting.
 * - Inventory: UAftermathInventoryComponent (server-owned, replicated to the owner only).
 */
UCLASS()
class AFTERMATH_API AAftermathCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AAftermathCharacter(const FObjectInitializer& ObjectInitializer);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;
	virtual void OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;

	virtual void OnRep_IsCrouched() override;

	UAftermathMovementComponent* GetAftermathMovement() const;

	/** Server: publish prone state for other players' proxies. */
	void SetReplicatedProne(bool bProne) { bReplicatedProne = bProne; }
	bool IsReplicatedProne() const { return bReplicatedProne; }

	// ---- Camera ----
	void ToggleCameraMode();
	bool IsFirstPerson() const { return bFirstPerson; }

	// ---- Stance input (owning client) ----
	void SetSprintInput(bool bPressed);
	void ToggleWalk();
	void ToggleCrouchStance();
	void ToggleProneStance();
	/** Space: vault if an obstacle is in front, otherwise jump. */
	void JumpOrVault();

	EAftermathStance GetStance() const;

	// ---- Interaction ----
	/** Owning client: what the crosshair is on (updated every tick). */
	AActor* GetFocusedInteractable() const { return FocusedActor.Get(); }
	FText GetFocusedPrompt() const;

	/** Owning client: request to use the focused object. */
	void TryInteract();

	/**
	 * Dev/test only: send the interact RPC for an arbitrary target, skipping the client-side focus and range
	 * check, to prove the server refuses out-of-reach requests from a modified client. Compiled out of Shipping.
	 */
	void DebugServerInteract(AActor* Target);

	/** Dev/test only: send a fire request with an arbitrary origin (server must reject bad ones). */
	void DebugServerFire(const FVector& Muzzle, const FVector& Direction);

	/**
	 * Dev/test only: ask the server for a loaded magazine. The server ignores this unless it was started
	 * with -AftermathAllowDebug (and always in Shipping), so it can't be used as a cheat on real servers.
	 */
	void DebugGiveLoadedMagazine(FName MagId, int32 Rounds);

	/** Server: validate reach and use Target. Public so tests and the GameMode can drive it. */
	bool ServerTryInteractWith(AActor* Target);

	/** Server + tests: is Target close enough to use right now? */
	bool IsWithinInteractReach(const AActor* Target) const;

	UPROPERTY(EditDefaultsOnly, Category = "Interaction")
	float InteractRange = 220.f;

	/** Extra server allowance for latency / movement between the client's trace and the RPC arriving. */
	UPROPERTY(EditDefaultsOnly, Category = "Interaction")
	float InteractServerTolerance = 60.f;

	// ---- Inventory / survival ----
	UAftermathInventoryComponent* GetInventory() const { return Inventory; }
	UAftermathSurvivalComponent* GetSurvival() const { return Survival; }
	UAftermathCombatComponent* GetCombat() const { return Combat; }
	bool IsDead() const;

	/** How far the third-person camera can sit from the eye (server accepts shot origins this far out). */
	float GetThirdPersonCameraReach() const;

	/** Owning client: zoom the camera in while aiming (narrower FOV, camera closer to the shoulder). */
	void SetAimZoom(bool bZoom);

	/** Owning client: use an item (eat, drink, bandage, wear, open can...). */
	void RequestUseItem(int32 Uid);
	/** Owning client: drag one item onto another (tool + can, knife + shirt, ammo + magazine, stack + stack). */
	void RequestCombineItems(int32 SourceUid, int32 TargetUid);
	void RequestUnloadMagazine(int32 Uid);

	virtual void Landed(const FHitResult& Hit) override;

	/** Eye position used for interaction traces and reach checks. */
	FVector GetInteractEyeLocation() const;

protected:
	virtual void BeginPlay() override;

	UFUNCTION(Server, Reliable)
	void ServerInteract(AActor* Target);

	UFUNCTION(Server, Reliable)
	void ServerUseItem(int32 Uid);

	UFUNCTION(Server, Reliable)
	void ServerCombineItems(int32 SourceUid, int32 TargetUid);

	UFUNCTION(Server, Reliable)
	void ServerUnloadMagazine(int32 Uid);

	UFUNCTION(Server, Reliable)
	void ServerDebugGiveMagazine(FName MagId, int32 Rounds);

	/** Stats and ailments. Simulated on the server, replicated to the owner only. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Survival")
	TObjectPtr<UAftermathSurvivalComponent> Survival;

	/** Firing, melee, reloading. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat")
	TObjectPtr<UAftermathCombatComponent> Combat;

	/** Dead bodies stop taking input and stay as a (grey-box) corpse until the GameMode cleans them up. */
	UPROPERTY(ReplicatedUsing = OnRep_Dead)
	bool bDead = false;

	UFUNCTION()
	void OnRep_Dead();

	void HandleDeath(AActor* Victim);
	void ApplyDeadState();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<UCameraComponent> FollowCamera;

	/** Placeholder body so other players are visible until real skeletal meshes arrive. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mesh")
	TObjectPtr<UStaticMeshComponent> PlaceholderBody;

	UPROPERTY(EditDefaultsOnly, Category = "Camera")
	float ThirdPersonArmLength = 250.f;

	UPROPERTY(EditDefaultsOnly, Category = "Camera")
	FVector ThirdPersonShoulderOffset = FVector(0.f, 50.f, 60.f);

	/** Eye height above the capsule centre when standing; scaled down with the capsule. */
	UPROPERTY(EditDefaultsOnly, Category = "Camera")
	float FirstPersonEyeForward = 10.f;

	/** Dev-only scripted driver for automated multiplayer tests (inert unless -AftermathScript is given). */
	UPROPERTY(VisibleAnywhere, Category = "Debug")
	TObjectPtr<class UAftermathTestScript> TestScript;

	/** Worn clothes and their cargo. Replicated to the owner only. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Inventory")
	TObjectPtr<UAftermathInventoryComponent> Inventory;

	/** Prone flag for simulated proxies. The owner predicts its own stance and skips this. */
	UPROPERTY(ReplicatedUsing = OnRep_ReplicatedProne)
	bool bReplicatedProne = false;

	UFUNCTION()
	void OnRep_ReplicatedProne();

private:
	void ApplyCameraMode();
	void UpdatePlaceholderBody();
	void UpdateFocus();
	IAftermathInteractable* ResolveInteractable(AActor* Actor) const;

	TWeakObjectPtr<AActor> FocusedActor;
	bool bFirstPerson = false;
};
