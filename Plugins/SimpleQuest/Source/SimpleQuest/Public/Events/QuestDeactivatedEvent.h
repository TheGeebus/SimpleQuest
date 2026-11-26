// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

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

	/** 
	 * Triggered by an external call - DeactivateQuest, editor tooling, or a game system  publishing FQuestDeactivateRequestEvent 
	 * on Tag_Channel_QuestDeactivateRequest. 
	 */
	External    UMETA(DisplayName = "External"),

	/**
	 * The questline's run was ended by End Questline Run. A teardown, not a gameplay transition: nothing wired to the node's
	 * Deactivated output fires. Observers can read it to tell "the run ended" apart from "the graph cut this off", for
	 * example to skip a failure message. Set by the framework; a request carrying it is not a supported way to deactivate.
	 */
	RunEnded    UMETA(DisplayName = "Run Ended"),
};

/** Log spelling for a deactivation source. */
inline const TCHAR* LexToString(const EDeactivationSource Source)
{
	switch (Source)
	{
	case EDeactivationSource::Internal: return TEXT("Internal");
	case EDeactivationSource::External: return TEXT("External");
	case EDeactivationSource::RunEnded: return TEXT("RunEnded");
	}
	return TEXT("Unknown");
}

USTRUCT(BlueprintType)
struct SIMPLEQUEST_API FQuestDeactivatedEvent : public FQuestEventBase
{
	GENERATED_BODY()

	FQuestDeactivatedEvent() = default;

	FQuestDeactivatedEvent(const FGameplayTag InQuestTag, const EDeactivationSource InSource)
		: FQuestEventBase(InQuestTag)
		, Source(InSource)
	{}

	FQuestDeactivatedEvent(const FGameplayTag InQuestTag, const EDeactivationSource InSource, const FQuestEventPayload& InContext)
		: FQuestEventBase(InQuestTag, InContext)
		, Source(InSource)
	{}

	UPROPERTY(BlueprintReadWrite)
	EDeactivationSource Source = EDeactivationSource::Internal;
};
