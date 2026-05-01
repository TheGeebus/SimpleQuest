// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Quests/Types/PrerequisiteExpression.h"

#include "SimpleQuestLog.h"
#include "WorldState/WorldStateSubsystem.h"
#include "Subsystems/QuestStateSubsystem.h"


bool FPrerequisiteExpression::Evaluate(const UWorldStateSubsystem* WorldState,
	const UQuestStateSubsystem* StateSubsystem) const
{
	if (Nodes.IsEmpty()) return true;
	return EvaluateNode(RootIndex, WorldState, StateSubsystem);
}

FQuestPrereqStatus FPrerequisiteExpression::EvaluateWithLeafStatus(const UWorldStateSubsystem* WorldState,
	const UQuestStateSubsystem* StateSubsystem) const
{
	FQuestPrereqStatus Status;

	if (Nodes.IsEmpty())
	{
		Status.bIsAlways = true;
		Status.bSatisfied = true;
		return Status;
	}

	Status.bIsAlways = false;

	// Walk per-node and emit one FQuestPrereqLeafStatus per leaf-typed node, branching on leaf kind. Replaces the
	// previous CollectLeafTags + per-tag iteration since mixed leaf types make the flat-tag-array shape lossy:
	// Leaf_Resolution leaves require the (QuestTag, OutcomeTag) pair, not just a single tag, to evaluate. LeafTag
	// is still emitted onto the status entry for display compatibility (matches the bridge tag stamped by the
	// compiler) so blocker-list rendering stays untouched through this batch.
	for (const FPrerequisiteExpressionNode& Node : Nodes)
	{
		if (Node.Type == EPrerequisiteExpressionType::Leaf)
		{
			FQuestPrereqLeafStatus LeafStatus;
			LeafStatus.LeafTag = Node.LeafTag;
			LeafStatus.bSatisfied = WorldState && Node.LeafTag.IsValid() && WorldState->HasFact(Node.LeafTag);
			Status.Leaves.Add(LeafStatus);
		}
		else if (Node.Type == EPrerequisiteExpressionType::Leaf_Resolution)
		{
			FQuestPrereqLeafStatus LeafStatus;
			LeafStatus.LeafTag = Node.LeafTag;  // bridge fact tag — preserves blocker-display API shape
			LeafStatus.bSatisfied = StateSubsystem
				&& Node.ResolutionQuestTag.IsValid()
				&& Node.ResolutionOutcomeTag.IsValid()
				&& StateSubsystem->HasResolvedWith(Node.ResolutionQuestTag, Node.ResolutionOutcomeTag);
			Status.Leaves.Add(LeafStatus);
		}
	}

	Status.bSatisfied = EvaluateNode(RootIndex, WorldState, StateSubsystem);

	return Status;
}

bool FPrerequisiteExpression::EvaluateNode(int32 NodeIndex, const UWorldStateSubsystem* WorldState,
	const UQuestStateSubsystem* StateSubsystem) const
{
	if (!Nodes.IsValidIndex(NodeIndex)) return true;
	const FPrerequisiteExpressionNode& Node = Nodes[NodeIndex];

	switch (Node.Type)
	{
	case EPrerequisiteExpressionType::Always:
		return true;

	case EPrerequisiteExpressionType::Leaf:
		{
			const bool bHas = WorldState && WorldState->HasFact(Node.LeafTag);
			UE_LOG(LogSimpleQuest, VeryVerbose, TEXT("Prereq leaf [Fact]: '%s' (valid=%d) → HasFact=%d"),
				*Node.LeafTag.ToString(), Node.LeafTag.IsValid(), bHas);
			return bHas;
		}

	case EPrerequisiteExpressionType::Leaf_Resolution:
		{
			const bool bResolved = StateSubsystem
				&& Node.ResolutionQuestTag.IsValid()
				&& Node.ResolutionOutcomeTag.IsValid()
				&& StateSubsystem->HasResolvedWith(Node.ResolutionQuestTag, Node.ResolutionOutcomeTag);
			UE_LOG(LogSimpleQuest, VeryVerbose, TEXT("Prereq leaf [Resolution]: quest='%s' outcome='%s' → HasResolvedWith=%d"),
				*Node.ResolutionQuestTag.ToString(), *Node.ResolutionOutcomeTag.ToString(), bResolved);
			return bResolved;
		}

	case EPrerequisiteExpressionType::And:
		for (int32 ChildIdx : Node.ChildIndices)
		{
			if (!EvaluateNode(ChildIdx, WorldState, StateSubsystem)) return false;
		}
		return true;

	case EPrerequisiteExpressionType::Or:
		for (int32 ChildIdx : Node.ChildIndices)
		{
			if (EvaluateNode(ChildIdx, WorldState, StateSubsystem)) return true;
		}
		return false;

	case EPrerequisiteExpressionType::Not:
		return Node.ChildIndices.Num() > 0 && !EvaluateNode(Node.ChildIndices[0], WorldState, StateSubsystem);
	}
	return true;
}

void FPrerequisiteExpression::CollectLeafTags(TArray<FGameplayTag>& OutTags) const
{
	if (Nodes.IsEmpty()) return;
	CollectLeafTagsFromNode(RootIndex, OutTags);
}

void FPrerequisiteExpression::CollectLeafTagsFromNode(int32 NodeIndex, TArray<FGameplayTag>& OutTags) const
{
	if (!Nodes.IsValidIndex(NodeIndex)) return;
	const FPrerequisiteExpressionNode& Node = Nodes[NodeIndex];

	// Both leaf kinds emit their LeafTag. Subscription wiring on the call sites continues to subscribe via the
	// FWorldStateFactAddedEvent channel.
	if (Node.Type == EPrerequisiteExpressionType::Leaf || Node.Type == EPrerequisiteExpressionType::Leaf_Resolution)
	{
		OutTags.AddUnique(Node.LeafTag);
		return;
	}
	for (int32 ChildIdx : Node.ChildIndices)
	{
		CollectLeafTagsFromNode(ChildIdx, OutTags);
	}
}

void FPrerequisiteExpression::CollectLeaves(TArray<FPrereqLeafDescriptor>& OutLeaves) const
{
	if (Nodes.IsEmpty()) return;
	CollectLeavesFromNode(RootIndex, OutLeaves);
}

void FPrerequisiteExpression::CollectLeavesFromNode(int32 NodeIndex, TArray<FPrereqLeafDescriptor>& OutLeaves) const
{
	if (!Nodes.IsValidIndex(NodeIndex)) return;
	const FPrerequisiteExpressionNode& Node = Nodes[NodeIndex];

	if (Node.Type == EPrerequisiteExpressionType::Leaf)
	{
		FPrereqLeafDescriptor Desc;
		Desc.Type = EPrerequisiteExpressionType::Leaf;
		Desc.FactTag = Node.LeafTag;
		OutLeaves.Add(Desc);
		return;
	}
	if (Node.Type == EPrerequisiteExpressionType::Leaf_Resolution)
	{
		FPrereqLeafDescriptor Desc;
		Desc.Type = EPrerequisiteExpressionType::Leaf_Resolution;
		Desc.ResolutionQuestTag = Node.ResolutionQuestTag;
		Desc.ResolutionOutcomeTag = Node.ResolutionOutcomeTag;
		OutLeaves.Add(Desc);
		return;
	}
	for (int32 ChildIdx : Node.ChildIndices)
	{
		CollectLeavesFromNode(ChildIdx, OutLeaves);
	}
}

void FPrerequisiteExpression::DebugDumpTo(TArray<FString>& OutLines, int32 NodeIndex, int32 Depth) const
{
	if (NodeIndex < 0) NodeIndex = RootIndex;
	if (!Nodes.IsValidIndex(NodeIndex))
	{
		OutLines.Add(FString::Printf(TEXT("%s<invalid idx %d>"), *FString::ChrN(Depth * 2, ' '), NodeIndex));
		return;
	}
	const FPrerequisiteExpressionNode& Node = Nodes[NodeIndex];
	const FString Indent = FString::ChrN(Depth * 2, ' ');
	switch (Node.Type)
	{
	case EPrerequisiteExpressionType::Always: OutLines.Add(Indent + TEXT("Always")); break;
	case EPrerequisiteExpressionType::Leaf:   OutLines.Add(FString::Printf(TEXT("%sLeaf [Fact] '%s' (valid=%d)"), *Indent, *Node.LeafTag.ToString(), Node.LeafTag.IsValid())); break;
	case EPrerequisiteExpressionType::Leaf_Resolution: OutLines.Add(FString::Printf(TEXT("%sLeaf [Resolution] quest='%s' outcome='%s' (bridge='%s')"),
		*Indent, *Node.ResolutionQuestTag.ToString(), *Node.ResolutionOutcomeTag.ToString(), *Node.LeafTag.ToString())); break;
	case EPrerequisiteExpressionType::And:    OutLines.Add(Indent + FString::Printf(TEXT("AND (%d children)"), Node.ChildIndices.Num())); break;
	case EPrerequisiteExpressionType::Or:     OutLines.Add(Indent + FString::Printf(TEXT("OR (%d children)"), Node.ChildIndices.Num())); break;
	case EPrerequisiteExpressionType::Not:    OutLines.Add(Indent + TEXT("NOT")); break;
	}
	for (int32 ChildIdx : Node.ChildIndices)
	{
		DebugDumpTo(OutLines, ChildIdx, Depth + 1);
	}
}
