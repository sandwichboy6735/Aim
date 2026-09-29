// Server-side anti-cheat: watches one player's aim and flags patterns that look like an aim bot.
// Add it to your player character (or your PlayerController, so the score survives respawns).
// Only the server's copy runs, because a cheater controls their own client.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AimAnalyzerCore.h"
#include "AimAnalyzerComponent.generated.h"

class APawn;

UENUM(BlueprintType)
enum class EAimDetection : uint8
{
	SnapToTarget,
	InhumanReaction,
	RoboticTracking,
	TrackingThroughWalls,
	ImpossibleAccuracy
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FAimDetectedSignature, EAimDetection, Detection, const FString&, Detail, float, Suspicion);

// Where a target's aim point was at one moment, for rewinding by the shooter's ping.
struct FLockOnPointSample
{
	double Time = 0.0;
	FVector Point = FVector::ZeroVector;
};

UCLASS(ClassGroup = (LockOnLab), meta = (BlueprintSpawnableComponent))
class UAimAnalyzerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UAimAnalyzerComponent();

	/** What counts as an enemy. Leave empty for any pawn except this player. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aim Analyzer|Targets")
	TSubclassOf<APawn> TargetClass;

	/** Pawns with any of these actor tags are ignored, such as "Dead". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aim Analyzer|Targets")
	TArray<FName> IgnoreTags;

	/** Head radius in cm. The checks scale with how big the head looks at that distance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aim Analyzer|Targets")
	float HeadRadius = 12.f;

	/** Targets slower than this, in cm/s, count as standing still. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aim Analyzer|Targets")
	float MovingSpeed = 50.f;

	/** Judge aim against where enemies were when the player saw them, by rewinding them by the player's ping. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aim Analyzer")
	bool bCompensateForPing = true;

	/** Show the suspicion score and detections on screen (development builds only). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aim Analyzer")
	bool bShowOnScreen = true;

	/** Snap check: the smallest one-update turn, in degrees, that can count as a snap. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aim Analyzer|Tuning")
	float SnapMinDegrees = 12.f;

	/** Reaction check: reaching a target faster than this after it appears is flagged. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aim Analyzer|Tuning")
	float ReactionMaxSeconds = 0.14f;

	/** Robotic check: aim wobble below this many head radii while tracking is flagged. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aim Analyzer|Tuning")
	float RoboticWobbleRadii = 0.1f;

	/** Wall check: following a hidden, moving target for this long is flagged. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aim Analyzer|Tuning")
	float WallMinSeconds = 0.8f;

	/** Accuracy check: hitting at least this share of the last 20 shots is flagged. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aim Analyzer|Tuning")
	float AccuracyThreshold = 0.9f;

	/** How fast the suspicion score cools off, in points per second. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aim Analyzer|Tuning")
	float SuspicionDecayPerSecond = 4.f;

	/** Fires on the server for every detection. Use it to log, record a replay, or flag the player for review. */
	UPROPERTY(BlueprintAssignable, Category = "Aim Analyzer")
	FAimDetectedSignature OnAimDetected;

	/** Call on the server each time this player fires, with whether the shot hit an enemy. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Aim Analyzer")
	void ReportShot(bool bHit);

	/** 0 to 100. Under 25 reads as Clean, 25 to 59 Suspicious, 60 or more Flagged. */
	UFUNCTION(BlueprintPure, Category = "Aim Analyzer")
	float GetSuspicion() const;

	UFUNCTION(BlueprintPure, Category = "Aim Analyzer")
	int32 GetDetectionCount(EAimDetection Detection) const;

	UFUNCTION(BlueprintCallable, Category = "Aim Analyzer")
	void ResetAnalysis();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void BeginPlay() override;

private:
	APawn* GetWatchedPawn() const;
	void ApplyTuning();
	void HandleDetections();

	aimcore::Analyzer Core;
	TMap<TWeakObjectPtr<APawn>, TArray<FLockOnPointSample>> History;
};
