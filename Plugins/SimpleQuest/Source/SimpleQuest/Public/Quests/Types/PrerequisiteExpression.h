// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "PrerequisiteExpression.generated.h"

class UWorldStateSubsystem;
class UQuestStateSubsystem;

UENUM()
enum class EPrerequisiteExpressionType : uint8
{
	Always,			 // no prereqs wired — trivially satisfied
	Leaf,			 // single WorldState fact (queries UWorldStateSubsystem::HasFact on LeafTag)
	And,			 // all children must be satisfied
	Or,				 // any child must be satisfied
	Not,			 // child must NOT be satisfied
	Leaf_Resolution, // Legacy outcome-keyed quest-resolution check (queries UQuestStateSubsystem::HasResolvedWith
					 // on LeafQuestTag and LeafOutcomeTag). Superseded by Leaf_Path (for pin-wired authoring) and
					 // Leaf_Outcome (for context-free outcome authoring); no current authoring path emits it.
					 // Retained for back-compat deserialization of assets compiled before the Path/Outcome split.
					 // Appended at the end so existing assets stamped with int values 0–4 continue to deserialize.
	Leaf_Entry,		 // Quest-entry check (queries UQuestStateSubsystem::HasEnteredWith on
					 // LeafQuestTag and LeafOutcomeTag). Appended at the end so existing
					 // serialized assets continue to deserialize correctly.
	Leaf_Path,		 // path-keyed quest-resolution check (queries UQuestStateSubsystem::HasResolvedAtPath on
					 // LeafQuestTag and LeafPathIdentity). Satisfies only when the named quest resolved
					 // through the specific authored path. This is what pin-wire prereq authoring emits —
					 // the designer wired a specific output pin into a Prereq input, so only that pin's
					 // resolution satisfies. Distinct from Leaf_Resolution: a quest with two paths sharing
					 // an outcome tag will satisfy Leaf_Resolution on either path's resolution, but
					 // Leaf_Path only on the named path's. Appended at the end so existing assets stamped
					 // with int values 0–6 continue to deserialize correctly.
	Leaf_Outcome	 // context-free outcome leaf — satisfies when ANY quest has resolved with LeafOutcomeTag
					 // (or any descendant via gameplay-tag hierarchy). Queries UQuestStateSubsystem::
					 // HasAnyQuestResolvedWith and subscribes directly on the outcome tag channel (Phase 6a
					 // outcome-channel publish target). Authored by the declarative PrerequisiteOutcome node;
					 // no quest-tag context required. Distinct from Leaf_Resolution which is quest-keyed.
					 // Appended at the end so existing assets stamped with int values 0–7 continue to deserialize.
};

USTRUCT(Blueprintable)
struct SIMPLEQUEST_API FPrerequisiteExpressionNode
{
	GENERATED_BODY()

	UPROPERTY() EPrerequisiteExpressionType Type = EPrerequisiteExpressionType::Always;

	/** Meaningful for Type=Leaf (the WorldState fact tag). Also populated for Type=Leaf_Resolution and Type=Leaf_Path
		as a bridge path-fact tag matching the runtime's MakeNodePathFact output for Prereq Examiner display
		compatibility: runtime evaluation reads via UQuestStateSubsystem rather than WorldState; the bridge tag is
		editor-side only. NOT populated for Type=Leaf_Entry. Entry leaves render via LeafQuestTag / LeafOutcomeTag
		directly. */
	UPROPERTY() FGameplayTag LeafTag;

	/** Source quest tag for Type=Leaf_Resolution, Type=Leaf_Entry, and Type=Leaf_Path. The runtime evaluator
		dispatches on Type to decide which UQuestStateSubsystem method to query: HasResolvedWith for resolution
		leaves (outcome-keyed), HasEnteredWith for entry leaves, HasResolvedAtPath for path leaves (path-keyed).
		Stamped by the compiler from the source content node's compiled tag. */
	UPROPERTY() FGameplayTag LeafQuestTag;

	/** Outcome tag for Type=Leaf_Resolution, Type=Leaf_Entry, and Type=Leaf_Outcome. NOT populated for
		Type=Leaf_Path — path leaves are path-keyed via LeafPathIdentity, not outcome-keyed. For Leaf_Outcome
		this is both the match criterion AND the subscribe channel (Phase 6a outcome-channel publish target). */
	UPROPERTY() FGameplayTag LeafOutcomeTag;

	/** Path identity for Type=Leaf_Path. Matches the source content node's output pin name (the pin's PathIdentity).
		Stamped by the compiler from the wired pin. NOT populated for any other leaf type. */
	UPROPERTY() FName LeafPathIdentity;

	UPROPERTY() TArray<int32> ChildIndices;
};


/**
 * Per-leaf evaluation result. One entry per Leaf node in the tree, in the order CollectLeafTags walks them.
 * Designers reading this can map each leaf back to its source content node via tag matching against compiled
 * ContextualTag hierarchy. Used by FQuestPrereqStatus.
 */
USTRUCT(BlueprintType)
struct SIMPLEQUEST_API FQuestPrereqLeafStatus
{
	GENERATED_BODY()

	/** The WorldState fact tag this leaf monitors. Matches the compiler's per-leaf fact output. */
	UPROPERTY(BlueprintReadOnly, Category = "Prereq")
	FGameplayTag LeafTag;

	/** True if WorldState->HasFact(LeafTag) returned true at evaluation time. */
	UPROPERTY(BlueprintReadOnly, Category = "Prereq")
	bool bSatisfied = false;
};

/**
 * Snapshot of a prerequisite expression's evaluation. Carries both the overall result and per-leaf detail so
 * designer-facing UI (giver indicators, dialogue dynamic text) can render contextual information about what
 * the player still needs to do.
 *
 * Built by FPrerequisiteExpression::EvaluateWithLeafStatus. Populated as part of FQuestActivatedEvent's payload
 * when the lifecycle redesign lands; also used by the activation-blocker query API for the PrereqUnmet case.
 */
USTRUCT(BlueprintType)
struct SIMPLEQUEST_API FQuestPrereqStatus
{
	GENERATED_BODY()

	/** True if the expression has no wired prereqs — trivially satisfiable. When true, Leaves is empty. */
	UPROPERTY(BlueprintReadOnly, Category = "Prereq")
	bool bIsAlways = true;

	/** Overall expression evaluation. True when bIsAlways is true, or when the wired expression evaluates true. */
	UPROPERTY(BlueprintReadOnly, Category = "Prereq")
	bool bSatisfied = true;

	/** Per-leaf evaluation. Empty when bIsAlways is true; otherwise one entry per Leaf node in the tree. */
	UPROPERTY(BlueprintReadOnly, Category = "Prereq")
	TArray<FQuestPrereqLeafStatus> Leaves;
};

/**
 * Per-leaf descriptor used by FPrerequisiteExpression::CollectLeaves to expose subscription-relevant identity for
 * each leaf with type discrimination. Plain (non-USTRUCT): only used internally by subscription wiring.
 *
 *  - Type=Leaf				—   FactTag is the WorldState fact to subscribe via FWorldStateFactAddedEvent /
 *								FWorldStateFactRemovedEvent on its tag channel.
 *  - Type=Leaf_Resolution	—	LeafQuestTag is the channel for FQuestResolutionRecordedEvent. LeafOutcomeTag
 *								is the match criterion: handler re-evaluates whether the quest has resolved with
 *								that outcome on any path. Legacy — no current authoring path emits this.
 *  - Type=Leaf_Entry		—	LeafQuestTag is the channel for FQuestEntryRecordedEvent. LeafOutcomeTag is the
 *								match criterion: handler re-evaluates whether the quest has been entered with that
 *								incoming outcome.
 *  - Type=Leaf_Path		—	LeafQuestTag is the channel for FQuestResolutionRecordedEvent (same channel as
 *								Leaf_Resolution). LeafPathIdentity is the match criterion: handler re-evaluates
 *								whether the quest has resolved through that specific authored path.
 *  - Type=Leaf_Outcome	—	LeafOutcomeTag is BOTH the channel (Phase 6a outcome-channel publish target for
 *								FQuestResolutionRecordedEvent) AND the match criterion. Context-free — no
 *								LeafQuestTag scoping; handler re-evaluates whether any quest has resolved with
 *								that outcome (or any descendant via gameplay-tag hierarchy).
 */
struct FPrereqLeafDescriptor
{
	EPrerequisiteExpressionType Type = EPrerequisiteExpressionType::Always;
	FGameplayTag FactTag;
	FGameplayTag LeafQuestTag;
	FGameplayTag LeafOutcomeTag;
	FName LeafPathIdentity;
};

USTRUCT(Blueprintable)
struct SIMPLEQUEST_API FPrerequisiteExpression
{
	GENERATED_BODY()

	UPROPERTY() TArray<FPrerequisiteExpressionNode> Nodes;
	UPROPERTY() int32 RootIndex = 0;

	bool IsAlways() const { return Nodes.IsEmpty(); }

	bool Evaluate(const UWorldStateSubsystem* WorldState, const UQuestStateSubsystem* StateSubsystem) const;

	FQuestPrereqStatus EvaluateWithLeafStatus(const UWorldStateSubsystem* WorldState,
		const UQuestStateSubsystem* StateSubsystem) const;

	void CollectLeafTags(TArray<FGameplayTag>& OutTags) const;

	void CollectLeaves(TArray<FPrereqLeafDescriptor>& OutLeaves) const;

	/**
	 * Builder methods. Return the index of the new node so callers can wire combinator children and set RootIndex.
	 * Use these instead of constructing FPrerequisiteExpressionNode by hand. Keeps per-leaf-kind field combinations
	 * consistent (Type + LeafTag for Leaf; Type + LeafQuestTag + LeafOutcomeTag + bridge LeafTag for
	 * Leaf_Resolution; Type + LeafQuestTag + LeafOutcomeTag for Leaf_Entry). Future leaf-kinds drop in as one
	 * new method here + one branch in the evaluator + one branch in the subscription helper.
	 */
	int32 AddAlways();
	int32 AddFactLeaf(const FGameplayTag& FactTag);
	int32 AddResolutionLeaf(FName NodeTagName, const FGameplayTag& OutcomeTag);
	int32 AddPathLeaf(FName NodeTagName, FName PathIdentity);
	int32 AddEntryLeaf(FName NodeTagName, const FGameplayTag& OutcomeTag);
	int32 AddOutcomeLeaf(const FGameplayTag& OutcomeTag);
	int32 AddCombinator(EPrerequisiteExpressionType Type);  // expects And or Or; child indices wired by caller
	int32 AddNot();                                         // child index wired by caller via AddCombinatorChild
	void  AddCombinatorChild(int32 ParentIndex, int32 ChildIndex);

private:
	bool EvaluateNode(int32 NodeIndex, const UWorldStateSubsystem* WorldState, const UQuestStateSubsystem* StateSubsystem) const;
	void CollectLeafTagsFromNode(int32 NodeIndex, TArray<FGameplayTag>& OutTags) const;
	void CollectLeavesFromNode(int32 NodeIndex, TArray<FPrereqLeafDescriptor>& OutLeaves) const;

public:
	void DebugDumpTo(TArray<FString>& OutLines, int32 NodeIndex = -1, int32 Depth = 0) const;
};

