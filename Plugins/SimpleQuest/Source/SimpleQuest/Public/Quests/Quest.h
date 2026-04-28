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
	 * Per-path, source-filtered entry routing. When this quest is entered via a specific completion path identity from a
	 * specific source, each FQuestEntryDestination in the matching list fires only if its SourceFilter matches the
	 * IncomingSourceTag passed by the activating parent. Compiler-written from the inner Entry node's exposed specs: one
	 * entry per spec; spec.Outcome.GetTagName() is the path identity for static placements.
	 */
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly)
	TMap<FName, FQuestEntryRouteList> EntryStepTagsByPath;

public:
	FORCEINLINE const TArray<FName>& GetEntryStepTags() const { return EntryStepTags; }
	FORCEINLINE const TMap<FName, FQuestEntryRouteList>& GetEntryStepTagsByPath() const { return EntryStepTagsByPath; }
};
