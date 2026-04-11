// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "QuestNodeBase.h"
#include "Quest.generated.h"

/**
 * Compiler-generated runtime node representing a quest container (act, chapter, or group of steps). Instantiated directly by
 * FQuestlineGraphCompiler, never authored as a Blueprint subclass. Contains the entry routing data for its inner graph:
 * which nodes to activate when this quest starts, optionally filtered by the incoming outcome from the parent graph.
 */
UCLASS(Blueprintable)
class SIMPLEQUEST_API UQuest : public UQuestNodeBase
{
	GENERATED_BODY()

	friend class FQuestlineGraphCompiler;

protected:
	/** Tags of nodes to activate when this quest starts (the "Any Outcome" path). Compiler-written from the inner Entry node's Any Outcome pin. */
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly)
	TArray<FName> EntryStepTags;

	/**
	 * Per-outcome entry routing. When this quest is entered via a specific outcome from the parent graph, the corresponding
	 * tags are activated in addition to EntryStepTags. Compiler-written from the inner Entry node's named outcome output pins.
	 */
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly)
	TMap<FGameplayTag, FQuestOutcomeNodeList> EntryStepTagsByOutcome;

public:
	FORCEINLINE const TArray<FName>& GetEntryStepTags() const { return EntryStepTags; }
	FORCEINLINE const TMap<FGameplayTag, FQuestOutcomeNodeList>& GetEntryStepTagsByOutcome() const { return EntryStepTagsByOutcome; }
};
