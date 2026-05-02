// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Quests/Types/PrerequisiteExpression.h"

/**
 * Test-only construction helpers for FPrerequisiteExpression. The production AddResolutionLeaf / AddEntryLeaf
 * builders resolve FName quest-tag identifiers via UGameplayTagsManager::RequestGameplayTag, which only
 * succeeds for tags already registered with the engine. Registering fixture tags pollutes the gameplay tag
 * picker for designers, so this header offers a parallel path: construct nodes directly with whatever
 * FGameplayTag values the test wants (commonly default-invalid, since structural assertions don't depend on
 * tag identity). Production builders are unchanged.
 *
 * Header-only and gated by WITH_DEV_AUTOMATION_TESTS so the symbol surface vanishes in shipping builds.
 * Mirrors the production builder set one-for-one: when a new builder lands, add a matching helper here.
 */
class FPrerequisiteExpressionTestHelpers
{
public:
	/** Append an Always node. Returns the new node's index. */
	static int32 AppendAlways(FPrerequisiteExpression& Expr)
	{
		FPrerequisiteExpressionNode Node;
		Node.Type = EPrerequisiteExpressionType::Always;
		return Expr.Nodes.Add(Node);
	}

	/** Append a fact-leaf node carrying FactTag. Returns the new node's index. */
	static int32 AppendFactLeaf(FPrerequisiteExpression& Expr, const FGameplayTag& FactTag)
	{
		FPrerequisiteExpressionNode Node;
		Node.Type = EPrerequisiteExpressionType::Leaf;
		Node.LeafTag = FactTag;
		return Expr.Nodes.Add(Node);
	}

	/** Append a Leaf_Resolution node with pre-built tag inputs. BridgeTag mirrors the bridge LeafTag the
	 *  production builder stamps for Prereq Examiner display compatibility: pass an invalid tag to leave
	 *  it empty. Returns the new node's index. */
	static int32 AppendResolutionLeaf(FPrerequisiteExpression& Expr,
		const FGameplayTag& QuestTag, const FGameplayTag& OutcomeTag, const FGameplayTag& BridgeTag = FGameplayTag())
	{
		FPrerequisiteExpressionNode Node;
		Node.Type = EPrerequisiteExpressionType::Leaf_Resolution;
		Node.LeafTag = BridgeTag;
		Node.LeafQuestTag = QuestTag;
		Node.LeafOutcomeTag = OutcomeTag;
		return Expr.Nodes.Add(Node);
	}

	/** Append a Leaf_Entry node with pre-built tag inputs. Entry leaves carry no bridge LeafTag: runtime
	 *  evaluation reads (LeafQuestTag, LeafOutcomeTag) directly via UQuestStateSubsystem::HasEnteredWith.
	 *  Returns the new node's index. */
	static int32 AppendEntryLeaf(FPrerequisiteExpression& Expr,
		const FGameplayTag& QuestTag, const FGameplayTag& OutcomeTag)
	{
		FPrerequisiteExpressionNode Node;
		Node.Type = EPrerequisiteExpressionType::Leaf_Entry;
		Node.LeafQuestTag = QuestTag;
		Node.LeafOutcomeTag = OutcomeTag;
		return Expr.Nodes.Add(Node);
	}

	/** Append an And or Or combinator. Caller wires children via AppendCombinatorChild. Returns the new node's
	 *  index. */
	static int32 AppendCombinator(FPrerequisiteExpression& Expr, EPrerequisiteExpressionType Type)
	{
		checkf(Type == EPrerequisiteExpressionType::And || Type == EPrerequisiteExpressionType::Or,
			TEXT("AppendCombinator only accepts And or Or; got %d"), static_cast<int32>(Type));
		FPrerequisiteExpressionNode Node;
		Node.Type = Type;
		return Expr.Nodes.Add(Node);
	}

	/** Append a Not node. Caller wires the child via AppendCombinatorChild. Returns the new node's index. */
	static int32 AppendNot(FPrerequisiteExpression& Expr)
	{
		FPrerequisiteExpressionNode Node;
		Node.Type = EPrerequisiteExpressionType::Not;
		return Expr.Nodes.Add(Node);
	}

	/** Wire ChildIndex as a child of ParentIndex. Mirrors FPrerequisiteExpression::AddCombinatorChild. */
	static void AppendCombinatorChild(FPrerequisiteExpression& Expr, int32 ParentIndex, int32 ChildIndex)
	{
		if (Expr.Nodes.IsValidIndex(ParentIndex) && ChildIndex != INDEX_NONE)
		{
			Expr.Nodes[ParentIndex].ChildIndices.Add(ChildIndex);
		}
	}
};

#endif // WITH_DEV_AUTOMATION_TESTS