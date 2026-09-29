// Shared by the lock-on test bot and the aim analyzer, so they agree on what a target is and where to aim.
#pragma once

#include "CoreMinimal.h"
#include "CollisionQueryParams.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"

namespace LockOnTargeting
{
	// The point the bot aims at and the analyzer measures against: the pawn's eyes.
	// It comes from replicated state only (location and eye height), so the client and the server
	// compute the same point. A head bone would differ between them, because servers don't animate.
	inline FVector GetAimPoint(const APawn* Pawn)
	{
		return Pawn->GetPawnViewLocation();
	}

	inline bool IsTarget(const APawn* Self, const APawn* Other, const TSubclassOf<APawn>& TargetClass, const TArray<FName>& IgnoreTags)
	{
		if (!IsValid(Other) || Other == Self)
		{
			return false;
		}
		const UClass* Class = TargetClass.Get();
		if (Class && !Other->IsA(Class))
		{
			return false;
		}
		for (const FName& Tag : IgnoreTags)
		{
			if (Other->ActorHasTag(Tag))
			{
				return false;
			}
		}
		return true;
	}

	inline bool HasLineOfSight(const UWorld* World, const APawn* Self, const APawn* Other, const FVector& From, const FVector& To)
	{
		FCollisionQueryParams Params(FName(TEXT("LockOnLineOfSight")), false, Self);
		Params.AddIgnoredActor(Other);
		return !World->LineTraceTestByChannel(From, To, ECC_Visibility, Params);
	}
}
