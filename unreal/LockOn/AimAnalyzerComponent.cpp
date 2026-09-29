#include "AimAnalyzerComponent.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "LockOnTargeting.h"

DEFINE_LOG_CATEGORY_STATIC(LogAimAnalyzer, Log, All);

namespace AimAnalyzerPrivate
{
	aimcore::Vec3 ToCore(const FVector& V)
	{
		return aimcore::Vec3{ V.X, V.Y, V.Z };
	}

	// Where the target was at Time, blending between the two samples around it. Samples are oldest first.
	FVector PointAt(const TArray<FLockOnPointSample>& Samples, double Time)
	{
		for (int32 Index = Samples.Num() - 1; Index > 0; --Index)
		{
			const FLockOnPointSample& Newer = Samples[Index];
			const FLockOnPointSample& Older = Samples[Index - 1];
			if (Older.Time <= Time)
			{
				const double Span = Newer.Time - Older.Time;
				const double Alpha = Span > 0.0 ? FMath::Clamp((Time - Older.Time) / Span, 0.0, 1.0) : 1.0;
				return FMath::Lerp(Older.Point, Newer.Point, Alpha);
			}
		}
		return Samples.Num() > 0 ? Samples[0].Point : FVector::ZeroVector;
	}
}

UAimAnalyzerComponent::UAimAnalyzerComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	IgnoreTags.Add(FName(TEXT("Dead")));
}

void UAimAnalyzerComponent::BeginPlay()
{
	Super::BeginPlay();

	// Only the server's copy analyzes. A cheater controls their own client, so results from it can't be trusted.
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		SetComponentTickEnabled(false);
	}
}

APawn* UAimAnalyzerComponent::GetWatchedPawn() const
{
	AActor* OwnerActor = GetOwner();
	if (APawn* OwnerPawn = Cast<APawn>(OwnerActor))
	{
		return OwnerPawn;
	}
	if (const AController* OwnerController = Cast<AController>(OwnerActor))
	{
		return OwnerController->GetPawn();
	}
	return nullptr;
}

void UAimAnalyzerComponent::ApplyTuning()
{
	aimcore::Config& Settings = Core.config;
	Settings.snapMinDeg = SnapMinDegrees;
	Settings.reactionMaxSeconds = ReactionMaxSeconds;
	Settings.roboticWobbleRadii = RoboticWobbleRadii;
	Settings.wallMinSeconds = WallMinSeconds;
	Settings.accuracyThreshold = AccuracyThreshold;
	Settings.decayPerSecond = SuspicionDecayPerSecond;
}

void UAimAnalyzerComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	APawn* Pawn = GetWatchedPawn();
	UWorld* World = GetWorld();
	if (!Pawn || !World)
	{
		return;
	}
	ApplyTuning();

	const double Now = World->GetTimeSeconds();
	double Rewind = 0.0;
	if (bCompensateForPing)
	{
		if (const APlayerState* PS = Pawn->GetPlayerState())
		{
			Rewind = PS->GetPingInMilliseconds() / 1000.0;
		}
	}

	const FVector Eye = LockOnTargeting::GetAimPoint(Pawn);
	const FVector AimDirection = Pawn->GetControlRotation().Vector();

	std::vector<aimcore::TargetSample> Targets;
	for (TActorIterator<APawn> It(World); It; ++It)
	{
		APawn* Other = *It;
		if (!LockOnTargeting::IsTarget(Pawn, Other, TargetClass, IgnoreTags))
		{
			continue;
		}

		// Keep a second of positions so the target can be rewound to what this player saw.
		TArray<FLockOnPointSample>& Samples = History.FindOrAdd(TWeakObjectPtr<APawn>(Other));
		if (Samples.Num() > 0 && Now - Samples.Last().Time > 0.5)
		{
			Samples.Reset();
		}
		FLockOnPointSample Latest;
		Latest.Time = Now;
		Latest.Point = LockOnTargeting::GetAimPoint(Other);
		Samples.Add(Latest);
		while (Samples.Num() > 2 && Now - Samples[0].Time > 1.0)
		{
			Samples.RemoveAt(0);
		}
		const FVector Point = AimAnalyzerPrivate::PointAt(Samples, Now - Rewind);

		aimcore::TargetSample Sample;
		Sample.id = static_cast<int>(Other->GetUniqueID());
		Sample.aimPoint = AimAnalyzerPrivate::ToCore(Point);
		Sample.radius = HeadRadius;
		Sample.visible = LockOnTargeting::HasLineOfSight(World, Pawn, Other, Eye, Point);
		Sample.moving = Other->GetVelocity().Size() > MovingSpeed;
		Targets.push_back(Sample);
	}
	for (auto It = History.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid() || It.Value().Num() == 0 || Now - It.Value().Last().Time > 2.0)
		{
			It.RemoveCurrent();
		}
	}

	Core.addSample(Now, AimAnalyzerPrivate::ToCore(Eye), AimAnalyzerPrivate::ToCore(AimDirection), Targets);
	HandleDetections();

#if !UE_BUILD_SHIPPING
	if (bShowOnScreen && GEngine)
	{
		const double Score = Core.suspicion();
		const TCHAR* Verdict = Score >= 60.0 ? TEXT("Flagged") : Score >= 25.0 ? TEXT("Suspicious") : TEXT("Clean");
		const FColor Color = Score >= 60.0 ? FColor::Red : Score >= 25.0 ? FColor::Yellow : FColor::Green;
		GEngine->AddOnScreenDebugMessage(static_cast<uint64>(GetUniqueID()), 0.5f, Color,
			FString::Printf(TEXT("Aim Analyzer - %s: suspicion %.0f (%s)"), *GetNameSafe(Pawn), Score, Verdict));
	}
#endif
}

void UAimAnalyzerComponent::ReportShot(bool bHit)
{
	const UWorld* World = GetWorld();
	if (!World || !GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	Core.addShot(World->GetTimeSeconds(), bHit);
	HandleDetections();
}

void UAimAnalyzerComponent::HandleDetections()
{
	for (const aimcore::Detection& Found : Core.takeDetections())
	{
		const EAimDetection Detection = static_cast<EAimDetection>(static_cast<int>(Found.kind));
		const FString Name = UTF8_TO_TCHAR(aimcore::kindName(Found.kind));
		const FString Detail = UTF8_TO_TCHAR(Found.detail.c_str());
		const float Score = static_cast<float>(Core.suspicion());
		const APawn* Pawn = GetWatchedPawn();
		const APlayerState* PS = Pawn ? Pawn->GetPlayerState() : nullptr;
		const FString Who = PS ? PS->GetPlayerName() : GetNameSafe(GetOwner());

		UE_LOG(LogAimAnalyzer, Warning, TEXT("%s: %s, %s. Suspicion %.0f"), *Who, *Name, *Detail, Score);
#if !UE_BUILD_SHIPPING
		if (bShowOnScreen && GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, FString::Printf(TEXT("Aim Analyzer - %s: %s, %s"), *Who, *Name, *Detail));
		}
#endif
		OnAimDetected.Broadcast(Detection, Detail, Score);
	}
}

float UAimAnalyzerComponent::GetSuspicion() const
{
	return static_cast<float>(Core.suspicion());
}

int32 UAimAnalyzerComponent::GetDetectionCount(EAimDetection Detection) const
{
	return Core.count(static_cast<aimcore::Kind>(static_cast<int>(Detection)));
}

void UAimAnalyzerComponent::ResetAnalysis()
{
	Core.reset();
	History.Reset();
}
