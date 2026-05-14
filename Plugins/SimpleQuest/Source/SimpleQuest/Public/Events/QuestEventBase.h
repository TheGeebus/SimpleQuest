// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include <concepts>

#include "Quests/Types/QuestEventPayload.h"
#include "QuestEventBase.generated.h"

USTRUCT(BlueprintType)
struct FQuestEventBase
{
	GENERATED_BODY()

	FQuestEventBase() = default;

	explicit FQuestEventBase(const FGameplayTag InQuestTag)
		: QuestTag(InQuestTag)
	{}

	FQuestEventBase(const FGameplayTag InQuestTag, const FQuestEventPayload& InPayload)
		: QuestTag(InQuestTag)
		, Payload(InPayload)
	{}
	
	UPROPERTY(BlueprintReadWrite)
	FGameplayTag QuestTag;

	UPROPERTY(BlueprintReadOnly)
	FQuestEventPayload Payload;
	
	FGameplayTag GetQuestTag() const { return QuestTag; }
};

/**
 * Concept satisfied by any struct deriving from FQuestEventBase. Use as a template-parameter constraint
 * (`template<CQuestEvent TEvent>` or `requires CQuestEvent<TEvent>`) wherever a public API needs to accept
 * "any quest event" without admitting unrelated types with compile time enforcement that generates readable errors.
 */
template<typename T>
concept CQuestEvent = std::derived_from<T, FQuestEventBase>;