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
	friend class UQuestManagerSubsystem;

protected:
	/** Tags of nodes to activate when this quest starts (the "Any Outcome" path). Compiler-written from the inner Entry node's Any Outcome pin. */
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly)
	TArray<FName> EntryStepTags;

	/**
	 * Per-path, source-filtered entry routing. ...
	 */
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly)
	TMap<FName, FQuestEntryRouteList> EntryStepTagsByPath;

	/**
	 * Per-cascade activation snapshots. Each call to UQuestManagerSubsystem::ActivateNodeByTag for this Quest
	 * appends one snapshot of PendingActivationParams to this queue. HandleOnNodeStarted drains the queue when
	 * ActivateInternal fires (immediately or via TryActivateDeferred) and fires entry routes for each queued
	 * cascade. Necessary for fan-in convergence patterns where multiple upstream outcomes feed into the same
	 * Quest while its prereq is unmet: without this queue, only the most-recent cascade's IncomingOutcomeTag
	 * survives when the prereq satisfies and earlier cascades' entry routes are silently dropped.
	 */
	UPROPERTY(Transient)
	TArray<FQuestObjectiveActivationParams> PendingEntryActivations;

	/** Clears the per-cascade queue between PIE sessions. Base ResetTransientState handles PendingActivationParams. */
	virtual void ResetTransientState() override;

public:
	FORCEINLINE const TArray<FName>& GetEntryStepTags() const { return EntryStepTags; }
	FORCEINLINE const TMap<FName, FQuestEntryRouteList>& GetEntryStepTagsByPath() const { return EntryStepTagsByPath; }
};
