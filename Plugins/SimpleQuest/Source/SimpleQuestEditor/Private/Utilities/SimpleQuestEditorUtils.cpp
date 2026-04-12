// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Utilities/SimpleQuestEditorUtils.h"

#include "SimpleQuestLog.h"
#include "K2Nodes/K2Node_CompleteObjectiveWithOutcome.h"
#include "Nodes/QuestlineNode_Exit.h"
#include "Objectives/QuestObjective.h"

class UQuestlineNode_Exit;

FString USimpleQuestEditorUtilities::SanitizeQuestlineTagSegment(const FString& InLabel)
{
	FString Result = InLabel.TrimStartAndEnd();
	for (TCHAR& Ch : Result)
	{
		if (!FChar::IsAlnum(Ch) && Ch != TEXT('_'))
			Ch = TEXT('_');
	}
	return Result;
}

TArray<FName> USimpleQuestEditorUtilities::CollectExitOutcomeTagNames(const UEdGraph* Graph)
{
	TArray<FName> Result;
	if (!Graph) return Result;
	for (const UEdGraphNode* Node : Graph->Nodes)
	{
		if (const UQuestlineNode_Exit* ExitNode = Cast<UQuestlineNode_Exit>(Node))
		{
			if (ExitNode->OutcomeTag.IsValid())
			{
				Result.AddUnique(ExitNode->OutcomeTag.GetTagName());
			}
		}
	}
	return Result;
}

TArray<FGameplayTag> USimpleQuestEditorUtilities::DiscoverObjectiveOutcomes(TSubclassOf<UQuestObjective> ObjectiveClass)
{
	if (!ObjectiveClass) return {};

	// Scan Blueprint graphs for K2 completion nodes
	if (UBlueprint* Blueprint = Cast<UBlueprint>(ObjectiveClass->ClassGeneratedBy))
	{
		TArray<FGameplayTag> Discovered;

		TArray<UEdGraph*> AllGraphs;
		Blueprint->GetAllGraphs(AllGraphs);

		for (UEdGraph* Graph : AllGraphs)
		{
			TArray<UK2Node_CompleteObjectiveWithOutcome*> Nodes;
			Graph->GetNodesOfClass(Nodes);

			for (const UK2Node_CompleteObjectiveWithOutcome* Node : Nodes)
			{
				FGameplayTag Tag = Node->OutcomeTag;
				if (Tag.IsValid())
				{
					Discovered.AddUnique(Tag);
				}
			}
		}

		// K2 nodes are authoritative when present; fall through only if none found
		if (Discovered.Num() > 0)
		{
			UE_LOG(LogSimpleQuest, Verbose, TEXT("DiscoverObjectiveOutcomes: Found %d outcome(s) via K2 nodes in %s"),
				Discovered.Num(), *ObjectiveClass->GetName());
			return Discovered;
		}
	}

	// Fallback: CDO's manually-set PossibleOutcomes (C++ classes, legacy Blueprints)
	if (const UQuestObjective* CDO = GetDefault<UQuestObjective>(ObjectiveClass))
	{
		return CDO->GetPossibleOutcomes();
	}

	return {};
}