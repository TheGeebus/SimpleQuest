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

public:
	virtual bool IsContainerNode() const override { return true; }
	
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

	/**
	 * All Steps reachable inside this container at any depth (own direct children + nested containers' Steps).
	 * Compile-time populated by FQuestlineGraphCompiler::ComputeContainerReachability. Read by the runtime
	 * container Live derivation: a container is Live if any tag in InnerStepTags has its Live fact set in
	 * WorldState (no inner Steps Live → container not Live).
	 */
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly)
	TArray<FGameplayTag> InnerStepTags;

	/**
	 * Per-Activate-pin Step reachability — keys mirror EntryStepTagsByPath plus NAME_None for the Any-Outcome pin.
	 * Compile-time populated by ComputeContainerReachability via a precise routing walk filtered by structural
	 * containment. Read by the path-aware giver gate: gate fires only if some reachable Step from the entered pin
	 * is not-yet-Live; if all reachable are already Live, the iteration has no work to enable so the gate skips.
	 */
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly)
	TMap<FName, FQuestReachableSteps> ReachableStepsByActivatePin;
	
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
	FORCEINLINE const TArray<FGameplayTag>& GetInnerStepTags() const { return InnerStepTags; }
	FORCEINLINE const TMap<FName, FQuestReachableSteps>& GetReachableStepsByActivatePin() const { return ReachableStepsByActivatePin; }
};
