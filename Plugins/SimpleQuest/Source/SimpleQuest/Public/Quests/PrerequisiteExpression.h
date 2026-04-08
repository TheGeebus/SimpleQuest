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

USTRUCT(Blueprintable)
struct SIMPLEQUEST_API FPrerequisiteExpression
{
	GENERATED_BODY()

	UPROPERTY() TArray<FPrerequisiteExpressionNode> Nodes;
	UPROPERTY() int32 RootIndex = 0;

	bool IsAlways() const { return Nodes.IsEmpty(); }

	/** Recursively evaluates the expression against the current WorldState. */
	bool Evaluate(const UWorldStateSubsystem* WorldState) const;

	/** Collects every leaf tag in the tree — the full subscription list for WorldState monitoring. */
	void CollectLeafTags(TArray<FGameplayTag>& OutTags) const;

private:
	bool EvaluateNode(int32 NodeIndex, const UWorldStateSubsystem* WorldState) const;
	void CollectLeafTagsFromNode(int32 NodeIndex, TArray<FGameplayTag>& OutTags) const;
};
