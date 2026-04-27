// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "PrerequisiteExpression.generated.h"

class UWorldStateSubsystem;

UENUM()
enum class EPrerequisiteExpressionType : uint8
{
	Always,		 // no prereqs wired — trivially satisfied
	Leaf,		 // single WorldState fact
	And,	     // all children must be satisfied
	Or,			 // any child must be satisfied
	Not			 // child must NOT be satisfied
};

USTRUCT(Blueprintable)
struct SIMPLEQUEST_API FPrerequisiteExpressionNode
{
	GENERATED_BODY()

	UPROPERTY() EPrerequisiteExpressionType Type = EPrerequisiteExpressionType::Always;
	UPROPERTY() FGameplayTag LeafTag;
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

USTRUCT(Blueprintable)
struct SIMPLEQUEST_API FPrerequisiteExpression
{
	GENERATED_BODY()

	UPROPERTY() TArray<FPrerequisiteExpressionNode> Nodes;
	UPROPERTY() int32 RootIndex = 0;

	bool IsAlways() const { return Nodes.IsEmpty(); }

	/** Recursively evaluates the expression against the current WorldState. */
	bool Evaluate(const UWorldStateSubsystem* WorldState) const;

	/**
	 * Single-walk evaluator that returns both the overall result and per-leaf evaluation detail. The full-tree
	 * Boolean evaluation matches Evaluate's semantics (And/Or/Not short-circuit); the per-leaf listing is the
	 * complete leaf set with each leaf's individual HasFact result. Designers consuming the per-leaf detail
	 * filter to !bSatisfied entries to know which prereqs the player still needs to satisfy.
	 */
	FQuestPrereqStatus EvaluateWithLeafStatus(const UWorldStateSubsystem* WorldState) const;
	
	/** Collects every leaf tag in the tree — the full subscription list for WorldState monitoring. */
	void CollectLeafTags(TArray<FGameplayTag>& OutTags) const;

private:
	bool EvaluateNode(int32 NodeIndex, const UWorldStateSubsystem* WorldState) const;
	void CollectLeafTagsFromNode(int32 NodeIndex, TArray<FGameplayTag>& OutTags) const;

public:
	void DebugDumpTo(TArray<FString>& OutLines, int32 NodeIndex = -1, int32 Depth = 0) const;
};

