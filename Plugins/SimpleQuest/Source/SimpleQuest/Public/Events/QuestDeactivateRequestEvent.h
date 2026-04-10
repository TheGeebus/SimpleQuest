// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "GameplayTagContainer.h"
#include "NativeGameplayTags.h"
#include "QuestEventBase.h"
#include "QuestDeactivateRequestEvent.generated.h"

UE_DECLARE_GAMEPLAY_TAG_EXTERN(Tag_Channel_QuestDeactivateRequest)

USTRUCT()
struct SIMPLEQUEST_API FQuestDeactivateRequestEvent : public FQuestEventBase
{
	GENERATED_BODY()
	
	FQuestDeactivateRequestEvent() = default;
	FQuestDeactivateRequestEvent(FGameplayTag InQuestTag, bool bInWriteBlocked)
		: FQuestEventBase(InQuestTag), bWriteBlocked(bInWriteBlocked) {}
	
	UPROPERTY()
	bool bWriteBlocked = false;
};

