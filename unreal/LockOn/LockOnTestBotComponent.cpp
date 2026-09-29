#include "LockOnTestBotComponent.h"

#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "LockOnTargeting.h"

#if !UE_BUILD_SHIPPING
static TAutoConsoleVariable<int32> CVarLockOnBotMode(
	TEXT("lockon.Mode"), -1,
	TEXT("Overrides the lock-on test bot's mode. -1 uses the component setting, 0 off, 1 snap, 2 smooth."),
	ECVF_Cheat);

static TAutoConsoleVariable<int32> CVarLockOnBotAlwaysOn(
	TEXT("lockon.AlwaysOn"), -1,
	TEXT("Overrides the lock-on test bot's Always On. -1 uses the component setting, 0 off, 1 on."),
	ECVF_Cheat);

static TAutoConsoleVariable<int32> CVarLockOnBotThroughWalls(
	TEXT("lockon.ThroughWalls"), -1,
	TEXT("Overrides the lock-on test bot's Through Walls. -1 uses the component setting, 0 off, 1 on."),
	ECVF_Cheat);
#endif

ULockOnTestBotComponent::ULockOnTestBotComponent()
{
	PrimaryComponentTick.bCanEverTick = !UE_BUILD_SHIPPING;
	HoldKey = EKeys::RightMouseButton;
	IgnoreTags.Add(FName(TEXT("Dead")));
}

void ULockOnTestBotComponent::SetLockHeld(bool bHeld)
{
	bLockHeld = bHeld;
}

APawn* ULockOnTestBotComponent::GetLockedTarget() const
{
	return Target.Get();
}

void ULockOnTestBotComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

#if !UE_BUILD_SHIPPING
	// Only the player's own machine aims. On the server and other clients this copy does nothing.
	APawn* Pawn = Cast<APawn>(GetOwner());
	if (!Pawn || !Pawn->IsLocallyControlled())
	{
		return;
	}
	APlayerController* PC = Cast<APlayerController>(Pawn->GetController());
	UWorld* World = GetWorld();
	if (!PC || !World)
	{
		return;
	}

	const int32 ModeOverride = CVarLockOnBotMode.GetValueOnGameThread();
	const ELockOnBotMode ActiveMode = ModeOverride >= 0 && ModeOverride <= 2 ? static_cast<ELockOnBotMode>(ModeOverride) : Mode;
	const int32 AlwaysOverride = CVarLockOnBotAlwaysOn.GetValueOnGameThread();
	const bool bAlways = AlwaysOverride >= 0 ? AlwaysOverride != 0 : bAlwaysOn;
	const int32 WallsOverride = CVarLockOnBotThroughWalls.GetValueOnGameThread();
	const bool bWalls = WallsOverride >= 0 ? WallsOverride != 0 : bThroughWalls;

	const bool bHolding = bLockHeld || (HoldKey.IsValid() && PC->IsInputKeyDown(HoldKey));
	if (ActiveMode == ELockOnBotMode::Off || !(bAlways || bHolding))
	{
		Target = nullptr;
		return;
	}

	const double Now = World->GetTimeSeconds();
	const FVector Eye = LockOnTargeting::GetAimPoint(Pawn);
	const FRotator Current = PC->GetControlRotation();

	auto Usable = [&](const APawn* Other)
	{
		if (!LockOnTargeting::IsTarget(Pawn, Other, TargetClass, IgnoreTags))
		{
			return false;
		}
		const FVector Point = LockOnTargeting::GetAimPoint(Other);
		if (FVector::Dist(Eye, Point) > MaxRange)
		{
			return false;
		}
		return bWalls || LockOnTargeting::HasLineOfSight(World, Pawn, Other, Eye, Point);
	};

	// Keep the current target while it's usable; otherwise pick the one closest to the crosshair.
	APawn* Locked = Target.Get();
	if (!Locked || !Usable(Locked))
	{
		Locked = nullptr;
		double BestAngle = MaxLockAngle;
		const FVector Forward = Current.Vector();
		for (TActorIterator<APawn> It(World); It; ++It)
		{
			APawn* Other = *It;
			if (!Usable(Other))
			{
				continue;
			}
			const FVector ToOther = (LockOnTargeting::GetAimPoint(Other) - Eye).GetSafeNormal();
			const double Angle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Forward, ToOther), -1.0, 1.0)));
			if (Angle < BestAngle)
			{
				BestAngle = Angle;
				Locked = Other;
			}
		}
		Target = Locked;
		OnTargetSince = -1.0;
		FireDelay = ActiveMode == ELockOnBotMode::Smooth ? 0.04 : 0.0;
	}
	if (!Locked)
	{
		return;
	}

	const FVector AimPoint = LockOnTargeting::GetAimPoint(Locked);
	const FRotator Desired = (AimPoint - Eye).Rotation();
	FRotator NewRotation = ActiveMode == ELockOnBotMode::Snap
		? Desired
		: FMath::RInterpConstantTo(Current, Desired, DeltaTime, 90.f + Strength * 630.f);
	NewRotation.Roll = Current.Roll;
	PC->SetControlRotation(NewRotation);

	if (!bAutoFire)
	{
		return;
	}
	const FVector ToAim = AimPoint - Eye;
	const double Distance = ToAim.Size();
	if (Distance < 1.0)
	{
		return;
	}
	const double ErrorDegrees = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(NewRotation.Vector(), ToAim / Distance), -1.0, 1.0)));
	const double HeadDegrees = FMath::RadiansToDegrees(FMath::Atan2(static_cast<double>(HeadRadius), Distance));
	if (ErrorDegrees < HeadDegrees * 0.6)
	{
		if (OnTargetSince < 0.0)
		{
			OnTargetSince = Now;
		}
		if (Now - OnTargetSince >= FireDelay && Now - LastFireTime >= FireInterval)
		{
			LastFireTime = Now;
			OnWantsToFire.Broadcast();
		}
	}
	else
	{
		OnTargetSince = -1.0;
	}
#endif
}
