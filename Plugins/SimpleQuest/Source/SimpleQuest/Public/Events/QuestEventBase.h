// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "Quests/Types/QuestEventContext.h"
#include "QuestEventBase.generated.h"

USTRUCT(BlueprintType)
struct FQuestEventBase
{
	GENERATED_BODY()

	FQuestEventBase() = default;

	explicit FQuestEventBase(const FGameplayTag InQuestTag)
		: QuestTag(InQuestTag)
	{}

	FQuestEventBase(const FGameplayTag InQuestTag, const FQuestEventContext& InContext)
		: QuestTag(InQuestTag)
		, Context(InContext)
	{}
	
	UPROPERTY(BlueprintReadWrite)
	FGameplayTag QuestTag;

	UPROPERTY(BlueprintReadOnly)
	FQuestEventContext Context;
	
	FGameplayTag GetQuestTag() const { return QuestTag; }
};