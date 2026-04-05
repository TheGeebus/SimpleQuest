// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "QuestNodeBase.h"
#include "QuestNode.generated.h"

/**
 * Compiler-generated runtime node representing a quest container (act, chapter, or group of steps). Instantiated directly by
 * FQuestlineGraphCompiler, never authored as a Blueprint subclass. UQuest is a temporary legacy subclass retained for backwards
 * compatibility during transition.
 *
 * When activated, starts the steps reachable from its inner Entry node. Completes when all inner paths reach an exit.
 */
UCLASS(Blueprintable)
class SIMPLEQUEST_API UQuestNode : public UQuestNodeBase
{
	GENERATED_BODY()

	friend class FQuestlineGraphCompiler;

protected:
	/** Tags of nodes to activate when this quest starts. Compiler-written from the inner Entry node connections. */
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly)
	TArray<FName> EntryStepTags;

public:
	FORCEINLINE const TArray<FName>& GetEntryStepTags() const { return EntryStepTags; }
};
