#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/AftermathInteractable.h"
#include "AftermathDoor.generated.h"

class UStaticMeshComponent;
class USceneComponent;

/**
 * Hinged door. Open state lives on the server and replicates; every machine rotates
 * the leaf locally toward that state (no per-frame transform replication).
 * The door swings away from whoever opens it, so it never sweeps into the user.
 */
UCLASS()
class AFTERMATH_API AAftermathDoor : public AActor, public IAftermathInteractable
{
	GENERATED_BODY()

public:
	AAftermathDoor();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void OnConstruction(const FTransform& Transform) override;

	// IAftermathInteractable
	virtual FText GetInteractPrompt(const AAftermathCharacter* User) const override;
	virtual FVector GetInteractLocation() const override;
	virtual void Interact(AAftermathCharacter* User) override;

	bool IsOpen() const { return OpenState != 0; }

	/** Server only. When opening, the leaf swings away from FromLocation. */
	void SetOpen(bool bNewOpen, const FVector& FromLocation);

	/** Current leaf yaw in degrees relative to the frame (0 = closed, sign = swing side). */
	float GetCurrentYaw() const { return CurrentYaw; }

protected:
	UPROPERTY(VisibleAnywhere, Category = "Door")
	TObjectPtr<USceneComponent> Hinge;

	UPROPERTY(VisibleAnywhere, Category = "Door")
	TObjectPtr<UStaticMeshComponent> Leaf;

	/** Door size in cm (width, thickness, height). Leaf is built from a 1m cube. */
	UPROPERTY(EditAnywhere, Category = "Door")
	FVector DoorSize = FVector(100.f, 8.f, 210.f);

	/** Opening angle magnitude in degrees. */
	UPROPERTY(EditAnywhere, Category = "Door")
	float OpenYaw = 95.f;

	UPROPERTY(EditAnywhere, Category = "Door")
	float SwingSpeedDegPerSec = 240.f;

	/** 0 = closed, +1 / -1 = open toward local +Y / -Y. */
	UPROPERTY(ReplicatedUsing = OnRep_Open, VisibleInstanceOnly, Category = "Door")
	int8 OpenState = 0;

	UFUNCTION()
	void OnRep_Open();

private:
	void ApplyLeafLayout();
	void ApplyYaw(float Yaw);

	float CurrentYaw = 0.f;
};
