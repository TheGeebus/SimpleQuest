// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Quests/PrerequisiteExpression.h"
#include "WorldState/WorldStateSubsystem.h"

bool FPrerequisiteExpression::Evaluate(const UWorldStateSubsystem* WorldState) const
{
	if (!WorldState || Nodes.IsEmpty()) return true;
	return EvaluateNode(RootIndex, WorldState);
}

bool FPrerequisiteExpression::EvaluateNode(int32 NodeIndex, const UWorldStateSubsystem* WorldState) const
{
	if (!Nodes.IsValidIndex(NodeIndex)) return true;
	const FPrerequisiteExpressionNode& Node = Nodes[NodeIndex];

	switch (Node.Type)
	{
	case EPrerequisiteExpressionType::Always:
		{
			return true;
		}
	case EPrerequisiteExpressionType::Leaf:
		{
			return WorldState->HasFact(Node.LeafTag);
		}
	case EPrerequisiteExpressionType::And:
		{
			for (int32 ChildIdx : Node.ChildIndices)
			{
				if (!EvaluateNode(ChildIdx, WorldState)) return false;
			}
			return true;
		}
	case EPrerequisiteExpressionType::Or:
		{
			for (int32 ChildIdx : Node.ChildIndices)
			{
				if (EvaluateNode(ChildIdx, WorldState)) return true;
			}
			return false;
		}
	case EPrerequisiteExpressionType::Not:
		{
			return Node.ChildIndices.Num() > 0 && !EvaluateNode(Node.ChildIndices[0], WorldState);
		}
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

	if (Node.Type == EPrerequisiteExpressionType::Leaf)
	{
		OutTags.AddUnique(Node.LeafTag);
		return;
	}
	for (int32 ChildIdx : Node.ChildIndices)
	{
		CollectLeafTagsFromNode(ChildIdx, OutTags);
	}
}
