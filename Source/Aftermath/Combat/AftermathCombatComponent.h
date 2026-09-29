#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Items/AftermathItemTypes.h"
#include "AftermathCombatComponent.generated.h"

class AAftermathCharacter;
class ACharacter;

/** Why the server refused a shot (logged, and counted for anti-cheat tuning). */
UENUM()
enum class EAftermathShotResult : uint8
{
	Fired,
	NoWeapon,
	NoAmmo,
	TooFast,
	BadOrigin,
	BadAim,
	Reloading,
	Dead
};

/** What a single bullet did (server). */
struct FAftermathShotOutcome
{
	EAftermathShotResult Result = EAftermathShotResult::NoWeapon;
	bool bHitCharacter = false;
	bool bHitWorld = false;
	/** Survivor or infected that was hit. */
	TWeakObjectPtr<ACharacter> Victim;
	EAftermathHitZone Zone = EAftermathHitZone::None;
	float Damage = 0.f;
	FVector ImpactPoint = FVector::ZeroVector;
	float Distance = 0.f;
};

/** Sent to shooter and victim for hit markers / damage direction. */
USTRUCT()
struct FAftermathHitNotify
{
	GENERATED_BODY()

	UPROPERTY() FVector_NetQuantize ImpactPoint;
	UPROPERTY() FVector_NetQuantize FromLocation;
	UPROPERTY() EAftermathHitZone Zone = EAftermathHitZone::None;
	UPROPERTY() float Damage = 0.f;
	UPROPERTY() bool bKilled = false;
};

/** Everyone nearby: muzzle flash + tracer + impact (cosmetic only). */
USTRUCT()
struct FAftermathShotFx
{
	GENERATED_BODY()

	UPROPERTY() FVector_NetQuantize Muzzle;
	UPROPERTY() FVector_NetQuantize Impact;
	UPROPERTY() bool bHitCharacter = false;
};

/**
 * Weapons in use: fire, melee, reload. Lives on the survivor.
 *
 * Flow for a shot:
 *   1. Owning client: presses fire, plays muzzle flash/recoil locally, sends ServerFire(muzzle, dir, seed, client time).
 *   2. Server: validates weapon, ammo, fire rate, muzzle near the eye, aim near the replicated view;
 *      spends a round; rewinds all other survivors to the client's shot time (capped); walks the bullet arc
 *      segment by segment against the world (walls stop it) and the rewound capsules; applies damage.
 *   3. Server -> shooter: hit marker. Server -> victim: damage direction. Server -> everyone near: tracer fx.
 *
 * The client never decides what it hit. Spread comes from a seed the client sends so both sides agree on
 * the cone offset, but the server re-derives and clamps the cone from its own weapon stats.
 */
namespace AftermathCombat
{
	/** Alive survivor or alive infected: something a bullet or a swing can hurt. */
	AFTERMATH_API bool IsHittable(const AActor* Actor);
}

UCLASS(ClassGroup = (Aftermath), meta = (BlueprintSpawnableComponent))
class AFTERMATH_API UAftermathCombatComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UAftermathCombatComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// ---- Owning client input ----
	void SetTriggerHeld(bool bHeld);
	void SetAiming(bool bAim);
	void RequestReload();
	bool IsAiming() const { return bAiming; }

	/** Def of what's in the hands (null if empty). */
	const FAftermathItemDef* GetHeldDef() const;
	/** Rounds in the attached magazine of the held gun (-1 if not a gun). */
	int32 GetHeldRounds() const;
	bool IsReloading() const { return bReloading; }

	// ---- Server entry points (RPC implementations call these; tests call them directly) ----
	FAftermathShotOutcome ServerHandleFire(const FVector& Muzzle, const FVector& Direction, int32 Seed, double ShotTime);
	bool ServerHandleMelee(const FVector& Direction);
	bool ServerHandleReload();

	/** Server: fists damage when nothing melee is held. */
	UPROPERTY(EditDefaultsOnly, Category = "Combat")
	float FistDamage = 8.f;

	/** Owning client: seconds since the last hit marker (HUD). */
	float GetHitMarkerAge() const { return HitMarkerAge; }
	bool WasLastHitKill() const { return bLastHitKill; }
	/** Owning client: direction we were last hit from (world), and how long ago. */
	FVector GetLastDamageFrom() const { return LastDamageFrom; }
	float GetDamageIndicatorAge() const { return DamageIndicatorAge; }

	/** All clients: recent tracers to draw. */
	struct FTracer { FVector From; FVector To; float Age = 0.f; bool bHit = false; };
	const TArray<FTracer>& GetTracers() const { return Tracers; }
	/** All clients: how many shot effects this component has received in total (tests; tracers fade fast). */
	int32 GetTracersReceived() const { return TracersReceived; }

	/** Dev/test: send the fire RPC with an arbitrary origin (to prove the server rejects it). */
	void DebugSendFire(const FVector& Muzzle, const FVector& Direction);

	/** Test hook: the last server outcome. */
	const FAftermathShotOutcome& GetLastOutcome() const { return LastOutcome; }

	/**
	 * Server: a hit lands on a survivor (armor, damage, wounds, damage-direction notify). Used by other
	 * survivors' weapons and by infected swings. Instigator is logged as the killer.
	 * Returns the notify (the attacking survivor forwards it as a hit marker).
	 */
	static FAftermathHitNotify ApplyHitToSurvivor(AAftermathCharacter& Victim, EAftermathHitZone Zone, float Damage, float Blood,
		float CutChance, const FVector& From, const FVector& Impact, const AActor* Instigator);

protected:
	virtual void BeginPlay() override;

	UFUNCTION(Server, Reliable)
	void ServerFire(FVector_NetQuantize Muzzle, FVector_NetQuantizeNormal Direction, int32 Seed, double ShotTime);

	UFUNCTION(Server, Reliable)
	void ServerMelee(FVector_NetQuantizeNormal Direction);

	UFUNCTION(Server, Reliable)
	void ServerReload();

	UFUNCTION(Client, Unreliable)
	void ClientHitConfirmed(const FAftermathHitNotify& Hit);

	UFUNCTION(Client, Unreliable)
	void ClientTookHit(const FAftermathHitNotify& Hit);

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastShotFx(const FAftermathShotFx& Fx);

	/** Replicated so every client can show "reloading" and block firing consistently. */
	UPROPERTY(Replicated)
	bool bReloading = false;

private:
	AAftermathCharacter* GetSurvivor() const;
	void ClientTryFire();
	void ClientTryMelee();
	/** Our hit landed on a survivor or an infected: apply it and show our hit marker. */
	void ApplyHit(ACharacter& Victim, EAftermathHitZone Zone, float Damage, float Blood, float CutChance,
		const FVector& From, const FVector& Impact);
	void FinishReload();

	// Client-side state
	bool bTriggerHeld = false;
	bool bAiming = false;
	bool bFiredThisPress = false;
	double LocalNextShotTime = 0.0;
	float HitMarkerAge = 99.f;
	bool bLastHitKill = false;
	FVector LastDamageFrom = FVector::ZeroVector;
	float DamageIndicatorAge = 99.f;
	TArray<FTracer> Tracers;
	int32 TracersReceived = 0;

	// Server-side state
	double ServerNextShotTime = 0.0;
	double ServerNextMeleeTime = 0.0;
	FTimerHandle ReloadTimer;
	FAftermathShotOutcome LastOutcome;
};
