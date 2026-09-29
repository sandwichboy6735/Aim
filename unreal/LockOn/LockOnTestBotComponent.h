// A lock-on aim bot that lives inside your game, for testing your anti-cheat.
// Add it to your player character. Its logic is compiled out of Shipping builds, so it never reaches players.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InputCoreTypes.h"
#include "LockOnTestBotComponent.generated.h"

class APawn;

UENUM(BlueprintType)
enum class ELockOnBotMode : uint8
{
	Off,
	Snap,   // jumps straight onto the target
	Smooth  // turns toward the target at a constant speed
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FLockOnBotWantsToFire);

UCLASS(ClassGroup = (LockOnLab), meta = (BlueprintSpawnableComponent))
class ULockOnTestBotComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	ULockOnTestBotComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lock-On Bot")
	ELockOnBotMode Mode = ELockOnBotMode::Smooth;

	/** Turn speed in Smooth mode: 0 is 90 degrees a second, 1 is 720. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lock-On Bot", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Strength = 0.5f;

	/** Hold this key to lock on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lock-On Bot")
	FKey HoldKey;

	/** Lock on without holding the key. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lock-On Bot")
	bool bAlwaysOn = false;

	/** Broadcast On Wants To Fire while the crosshair is on the target's head. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lock-On Bot")
	bool bAutoFire = true;

	/** Seconds between auto-fire shots. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lock-On Bot", meta = (ClampMin = "0.01"))
	float FireInterval = 0.15f;

	/** Lock onto targets behind walls too, to test the analyzer's wall check. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lock-On Bot")
	bool bThroughWalls = false;

	/** What counts as a target. Leave empty for any pawn except yourself. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lock-On Bot|Targets")
	TSubclassOf<APawn> TargetClass;

	/** Pawns with any of these actor tags are skipped, such as "Dead". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lock-On Bot|Targets")
	TArray<FName> IgnoreTags;

	/** Only lock onto targets within this many degrees of where you're looking. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lock-On Bot|Targets", meta = (ClampMin = "1.0", ClampMax = "180.0"))
	float MaxLockAngle = 35.f;

	/** Only lock onto targets closer than this, in cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lock-On Bot|Targets")
	float MaxRange = 10000.f;

	/** Head radius in cm, used to decide when the crosshair is on target. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lock-On Bot|Targets")
	float HeadRadius = 12.f;

	/** Connect this to your fire function (the one your shoot input calls). */
	UPROPERTY(BlueprintAssignable, Category = "Lock-On Bot")
	FLockOnBotWantsToFire OnWantsToFire;

	/** Hold the lock from your own input (gamepad, Enhanced Input action) instead of Hold Key. */
	UFUNCTION(BlueprintCallable, Category = "Lock-On Bot")
	void SetLockHeld(bool bHeld);

	UFUNCTION(BlueprintPure, Category = "Lock-On Bot")
	APawn* GetLockedTarget() const;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	bool bLockHeld = false;
	TWeakObjectPtr<APawn> Target;
	double OnTargetSince = -1.0;
	double FireDelay = 0.0;
	double LastFireTime = -1000.0;
};
