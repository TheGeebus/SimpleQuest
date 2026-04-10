// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Events/QuestEventBase.h"
#include "QuestDeactivatedEvent.generated.h"

UENUM(BlueprintType)
enum class EDeactivationSource : uint8
{
	/** Triggered by a Deactivate input pin, SetBlocked node, or other graph-authored path. */
	Internal    UMETA(DisplayName = "Internal"),

	/** Triggered by an external call — AbandonQuest, editor tooling, or a game system  publishing FAbandonQuestEvent on Tag_Channel_QuestAbandoned. */
	External    UMETA(DisplayName = "External"),
};

USTRUCT(BlueprintType)
struct SIMPLEQUEST_API FQuestDeactivatedEvent : public FQuestEventBase
{
	GENERATED_BODY()

	FQuestDeactivatedEvent() = default;

	FQuestDeactivatedEvent(const FGameplayTag InQuestTag, const EDeactivationSource InSource)
		: FQuestEventBase(InQuestTag)
		, Source(InSource)
	{}

	UPROPERTY(BlueprintReadWrite)
	EDeactivationSource Source = EDeactivationSource::Internal;
};
