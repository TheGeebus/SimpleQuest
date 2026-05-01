// Copyright 2026, Greg Bussell, All Rights Reserved.

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
	Leaf_Resolution	 // quest-resolution check (queries UQuestStateSubsystem::HasResolvedWith on
					 // ResolutionQuestTag and ResolutionOutcomeTag). Appended at the end so existing
					 // assets stamped with int values 0–4 continue to deserialize correctly.
};

USTRUCT(Blueprintable)
struct SIMPLEQUEST_API FPrerequisiteExpressionNode
{
	GENERATED_BODY()

	UPROPERTY() EPrerequisiteExpressionType Type = EPrerequisiteExpressionType::Always;

	/** Meaningful for Type=Leaf (the WorldState fact tag). Also populated for Type=Leaf_Resolution as a bridge
		path-fact tag matching the runtime's MakeNodePathFact output, so the legacy fact-channel subscription
		wiring (CollectLeafTags and RegisterEnablementWatch) keeps working unchanged through Batch B of the
		Outcome/Path data-layer migration. Batch C cuts the subscription wiring over to FQuestResolutionRecorded-
		Event and stops populating LeafTag for Leaf_Resolution leaves. */
	UPROPERTY() FGameplayTag LeafTag;

	/** Meaningful only for Type=Leaf_Resolution. The (QuestTag, OutcomeTag) pair the leaf checks against the
		UQuestStateSubsystem registry via HasResolvedWith. Stamped by the compiler from the outcome-pin context. */
	UPROPERTY() FGameplayTag ResolutionQuestTag;
	UPROPERTY() FGameplayTag ResolutionOutcomeTag;

	UPROPERTY() TArray<int32> ChildIndices;
};


/**
 * Per-leaf evaluation result. One entry per Leaf node in the tree, in the order CollectLeafTags walks them.
 * Designers reading this can map each leaf back to its source content node via tag matching against compiled
 * QuestTag hierarchy. Used by FQuestPrereqStatus.
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
 *  - Type=Leaf_Resolution	—	ResolutionQuestTag is the channel for FQuestResolutionRecordedEvent. ResolutionOutcomeTag
 *								is informational — handler unconditionally re-evaluates the expression, which uses the
 *								outcome internally via UQuestStateSubsystem::HasResolvedWith.
 */
struct FPrereqLeafDescriptor
{
	EPrerequisiteExpressionType Type = EPrerequisiteExpressionType::Always;
	FGameplayTag FactTag;
	FGameplayTag ResolutionQuestTag;
	FGameplayTag ResolutionOutcomeTag;
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
	 * consistent (Type + LeafTag for Leaf; Type + ResolutionQuestTag + ResolutionOutcomeTag + bridge LeafTag for
	 * Leaf_Resolution). Future leaf-kinds drop in as one new method here + one branch in the evaluator + one branch
	 * in the subscription helper.
	 */
	int32 AddAlways();
	int32 AddFactLeaf(const FGameplayTag& FactTag);
	int32 AddResolutionLeaf(FName NodeTagName, const FGameplayTag& OutcomeTag);
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

