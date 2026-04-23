// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Quests/PrerequisiteExpression.h"

#include "SimpleQuestLog.h"
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
			const bool bHas = WorldState->HasFact(Node.LeafTag);
			UE_LOG(LogSimpleQuest, VeryVerbose, TEXT("Prereq leaf: '%s' (valid=%d) → HasFact=%d"),
				*Node.LeafTag.ToString(), Node.LeafTag.IsValid(), bHas);

			return bHas;
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
	case EPrerequisiteExpressionType::Leaf:   OutLines.Add(FString::Printf(TEXT("%sLeaf '%s' (valid=%d)"), *Indent, *Node.LeafTag.ToString(), Node.LeafTag.IsValid())); break;
	case EPrerequisiteExpressionType::And:    OutLines.Add(Indent + FString::Printf(TEXT("AND (%d children)"), Node.ChildIndices.Num())); break;
	case EPrerequisiteExpressionType::Or:     OutLines.Add(Indent + FString::Printf(TEXT("OR (%d children)"), Node.ChildIndices.Num())); break;
	case EPrerequisiteExpressionType::Not:    OutLines.Add(Indent + TEXT("NOT")); break;
	}
	for (int32 ChildIdx : Node.ChildIndices)
	{
		DebugDumpTo(OutLines, ChildIdx, Depth + 1);
	}
}
