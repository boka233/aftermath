#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/RootMotionSource.h"
#include "Movement/AftermathMovementMath.h"
#include "AftermathMovementComponent.generated.h"

/**
 * Root motion source that follows an FAftermathVaultPath at constant speed.
 * Path parameters are net-serialized, so the server replays the exact trajectory the client predicted.
 */
USTRUCT()
struct AFTERMATH_API FAftermathRootMotionSource_Vault : public FRootMotionSource
{
	GENERATED_BODY()

	FAftermathRootMotionSource_Vault();
	virtual ~FAftermathRootMotionSource_Vault() = default;

	UPROPERTY()
	FVector PathStart = FVector::ZeroVector;

	UPROPERTY()
	FVector PathForward = FVector::ForwardVector;

	/** RiseEnd, HoldEnd, Length (horizontal distances along PathForward). */
	UPROPERTY()
	FVector PathDistances = FVector::ZeroVector;

	/** StartZ, PeakZ, EndZ. */
	UPROPERTY()
	FVector PathHeights = FVector::ZeroVector;

	void SetPath(const FAftermathVaultPath& Path);
	FAftermathVaultPath GetPath() const;

	virtual FRootMotionSource* Clone() const override;
	virtual bool Matches(const FRootMotionSource* Other) const override;
	virtual bool UpdateStateFrom(const FRootMotionSource* SourceToTakeStateFrom, bool bMarkForSimulatedCatchup = false) override;
	virtual void PrepareRootMotion(float SimulationTime, float MovementTickTime,
		const ACharacter& Character, const UCharacterMovementComponent& MoveComponent) override;
	virtual bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess) override;
	virtual UScriptStruct* GetScriptStruct() const override;
	virtual FString ToSimpleString() const override;
};

template<>
struct TStructOpsTypeTraits<FAftermathRootMotionSource_Vault> : public TStructOpsTypeTraitsBase2<FAftermathRootMotionSource_Vault>
{
	enum
	{
		WithNetSerializer = true,
		WithCopy = true
	};
};

/** Result of a vault feasibility query. Pure data; used by prediction, server, HUD and tests. */
struct AFTERMATH_API FAftermathVaultQuery
{
	bool bCanVault = false;
	FString FailReason;
	float ObstacleHeight = 0.f;
	FAftermathVaultPath Path;
};

/**
 * Survivor movement: jog / walk / sprint / crouch / prone, and vaulting low obstacles.
 *
 * Networking: the owning client sets intent flags (sprint/walk/crouch/prone/vault), they ride in the
 * saved-move compressed flags, and BOTH client and server run the same simulation. The server's result
 * is authoritative; mispredictions are corrected by the standard CMC replay.
 *
 * Stance is derived from the actual capsule height, so a blocked stand-up keeps you low on every machine.
 */
UCLASS()
class AFTERMATH_API UAftermathMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

	friend class FSavedMove_Aftermath;

public:
	UAftermathMovementComponent();

	// ---- Intent (set on the owning client, replicated through saved moves) ----
	void SetWantsSprint(bool bValue) { bWantsSprint = bValue; }
	void SetWantsWalk(bool bValue)   { bWantsWalk = bValue; }
	/** Requests prone. Crouch and prone are mutually exclusive; prone wins. */
	void SetWantsProne(bool bValue)  { bWantsProne = bValue; if (bValue) { bWantsToCrouch = false; } }
	void SetWantsCrouchStance(bool bValue) { bWantsToCrouch = bValue; if (bValue) { bWantsProne = false; } }
	void RequestVault()              { bWantsVault = true; }

	bool WantsSprint() const { return bWantsSprint; }
	bool WantsWalk() const   { return bWantsWalk; }
	bool WantsProne() const  { return bWantsProne; }

	// ---- Derived state ----
	EAftermathStance GetStance() const;
	bool IsProne() const;
	bool IsVaulting() const;
	bool IsSprinting() const;

	/**
	 * Simulated proxies only: resize the capsule from replicated state. The engine only replicates
	 * bIsCrouched, so prone travels in a separate flag and both are applied together here.
	 */
	void ApplyReplicatedStance(bool bCrouched, bool bProne);

	/** Checks whether a vault is possible right now from the current capsule. Const, no side effects. */
	FAftermathVaultQuery QueryVault() const;

	// ---- UCharacterMovementComponent ----
	virtual float GetMaxSpeed() const override;
	virtual float GetMaxAcceleration() const override;
	virtual bool CanAttemptJump() const override;
	virtual bool CanCrouchInCurrentState() const override;
	virtual void UpdateCharacterStateBeforeMovement(float DeltaSeconds) override;
	virtual void UpdateCharacterStateAfterMovement(float DeltaSeconds) override;
	virtual FNetworkPredictionData_Client* GetPredictionData_Client() const override;

	UPROPERTY(EditDefaultsOnly, Category = "Aftermath|Speed")
	FAftermathSpeedConfig Speeds;

	UPROPERTY(EditDefaultsOnly, Category = "Aftermath|Stance")
	float ProneHalfHeight = 35.f;

	/** Obstacles between these heights (cm above feet) can be vaulted. */
	UPROPERTY(EditDefaultsOnly, Category = "Aftermath|Vault")
	float VaultMinHeight = 40.f;

	UPROPERTY(EditDefaultsOnly, Category = "Aftermath|Vault")
	float VaultMaxHeight = 130.f;

	/** How far ahead of the capsule surface the obstacle's front face may be. */
	UPROPERTY(EditDefaultsOnly, Category = "Aftermath|Vault")
	float VaultReach = 70.f;

	/** Thickest obstacle that can be vaulted (front face to back face). */
	UPROPERTY(EditDefaultsOnly, Category = "Aftermath|Vault")
	float VaultMaxDepth = 90.f;

	/** Extra gap above the obstacle top during the vault. */
	UPROPERTY(EditDefaultsOnly, Category = "Aftermath|Vault")
	float VaultClearance = 8.f;

	/** Speed along the vault path (cm/s). */
	UPROPERTY(EditDefaultsOnly, Category = "Aftermath|Vault")
	float VaultSpeed = 380.f;

	/** Vault root motion instance name (one active at a time). */
	static const FName VaultInstanceName;

protected:
	virtual void UpdateFromCompressedFlags(uint8 Flags) override;

private:
	/** Resize the capsule toward the target stance. Returns true if the capsule is at the target height after the call. */
	bool TrySetStanceHalfHeight(float TargetHalfHeight);
	void CrouchToHalfHeight(float TargetHalfHeight, bool bClientSimulation);
	bool IsCapsuleAtHalfHeight(float HalfHeight) const;
	float GetDefaultHalfHeight() const;
	void StartVault(const FAftermathVaultPath& Path);

	// Intent. Not UPROPERTY-replicated: they travel inside saved moves.
	uint8 bWantsSprint : 1;
	uint8 bWantsWalk : 1;
	uint8 bWantsProne : 1;
	uint8 bWantsVault : 1;
};

/** Saved move carrying the Aftermath intent flags. */
class AFTERMATH_API FSavedMove_Aftermath : public FSavedMove_Character
{
public:
	using Super = FSavedMove_Character;

	// Engine reserves 0x10-0x80 for games.
	enum : uint8
	{
		FLAG_Sprint = FLAG_Custom_0,
		FLAG_Walk   = FLAG_Custom_1,
		FLAG_Prone  = FLAG_Custom_2,
		FLAG_Vault  = FLAG_Custom_3,
	};

	virtual void Clear() override;
	virtual uint8 GetCompressedFlags() const override;
	virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* InCharacter, float MaxDelta) const override;
	virtual void SetMoveFor(ACharacter* C, float InDeltaTime, FVector const& NewAccel, FNetworkPredictionData_Client_Character& ClientData) override;
	virtual void PrepMoveFor(ACharacter* C) override;

	uint8 bSavedWantsSprint : 1 = 0;
	uint8 bSavedWantsWalk : 1 = 0;
	uint8 bSavedWantsProne : 1 = 0;
	uint8 bSavedWantsVault : 1 = 0;
};

class AFTERMATH_API FNetworkPredictionData_Client_Aftermath : public FNetworkPredictionData_Client_Character
{
public:
	using Super = FNetworkPredictionData_Client_Character;

	explicit FNetworkPredictionData_Client_Aftermath(const UCharacterMovementComponent& ClientMovement)
		: Super(ClientMovement)
	{
	}

	virtual FSavedMovePtr AllocateNewMove() override
	{
		return FSavedMovePtr(new FSavedMove_Aftermath());
	}
};
