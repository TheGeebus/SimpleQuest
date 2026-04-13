// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Utilities/SimpleQuestEditorUtils.h"

#include "SimpleQuestLog.h"
#include "K2Nodes/K2Node_CompleteObjectiveWithOutcome.h"
#include "Nodes/QuestlineNode_Exit.h"
#include "Objectives/QuestObjective.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Nodes/QuestlineNode_Step.h"
#include "Nodes/QuestlineNode_Quest.h"
#include "Quests/QuestlineGraph.h"
#include "Components/QuestTargetComponent.h"


class UQuestlineNode_Exit;


FString USimpleQuestEditorUtilities::SanitizeQuestlineTagSegment(const FString& InLabel)
{
	FString Result = InLabel.TrimStartAndEnd();
	for (TCHAR& Ch : Result)
	{
		if (!FChar::IsAlnum(Ch) && Ch != TEXT('_'))
		{
			Ch = TEXT('_');
		}
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

	TArray<FGameplayTag> AllOutcomes;

	// ── Source 1: K2 node scan (Blueprint graphs) ──
	if (UBlueprint* Blueprint = Cast<UBlueprint>(ObjectiveClass->ClassGeneratedBy))
	{
		TArray<UEdGraph*> AllGraphs;
		Blueprint->GetAllGraphs(AllGraphs);
		for (UEdGraph* Graph : AllGraphs)
		{
			TArray<UK2Node_CompleteObjectiveWithOutcome*> Nodes;
			Graph->GetNodesOfClass(Nodes);
			for (const UK2Node_CompleteObjectiveWithOutcome* Node : Nodes)
			{
				if (Node->OutcomeTag.IsValid())	AllOutcomes.AddUnique(Node->OutcomeTag);
			}
		}
	}

	// ── Source 2: UPROPERTY reflection scan (ObjectiveOutcome meta) ──
	if (const UQuestObjective* CDO = GetDefault<UQuestObjective>(ObjectiveClass))
	{
		for (TFieldIterator<FStructProperty> PropIt(ObjectiveClass); PropIt; ++PropIt)
		{
			if (PropIt->Struct == FGameplayTag::StaticStruct()
				&& PropIt->HasMetaData(TEXT("ObjectiveOutcome")))
			{
				const FGameplayTag* Tag = PropIt->ContainerPtrToValuePtr<FGameplayTag>(CDO);
				if (Tag && Tag->IsValid())
				{
					AllOutcomes.AddUnique(*Tag);
				}
			}
		}

		// ── Source 3: Virtual GetPossibleOutcomes (programmatic / legacy) ──
		for (const FGameplayTag& Tag : CDO->GetPossibleOutcomes())
		{
			if (Tag.IsValid())
			{
				AllOutcomes.AddUnique(Tag);
			}
		}
	}

	if (AllOutcomes.Num() > 0)
	{
		UE_LOG(LogSimpleQuest, Verbose,	TEXT("DiscoverObjectiveOutcomes: Found %d outcome(s) for %s"),	AllOutcomes.Num(), *ObjectiveClass->GetName());
	}

	return AllOutcomes;
}
FGameplayTag USimpleQuestEditorUtilities::ReconstructStepTag(const UQuestlineNode_Step* StepNode)
{
	if (!StepNode) return FGameplayTag();

	const FString StepLabel = SanitizeQuestlineTagSegment(
		StepNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
	if (StepLabel.IsEmpty()) return FGameplayTag();

	UEdGraph* CurrentGraph = StepNode->GetGraph();
	if (!CurrentGraph) return FGameplayTag();

	// Walk up the outer chain, collecting labels from any enclosing Quest nodes
	TArray<FString> Segments;
	Segments.Add(StepLabel);

	UObject* Outer = CurrentGraph->GetOuter();

	while (UQuestlineNode_Quest* QuestNode = Cast<UQuestlineNode_Quest>(Outer))
	{
		const FString QuestLabel = SanitizeQuestlineTagSegment(
			QuestNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
		if (QuestLabel.IsEmpty()) return FGameplayTag();

		Segments.Insert(QuestLabel, 0);

		UEdGraph* QuestGraph = QuestNode->GetGraph();
		if (!QuestGraph) return FGameplayTag();
		Outer = QuestGraph->GetOuter();
	}

	// The final outer should be the UQuestlineGraph asset
	const UQuestlineGraph* QuestlineAsset = Cast<UQuestlineGraph>(Outer);
	if (!QuestlineAsset) return FGameplayTag();

	const FString& QuestlineID = QuestlineAsset->GetQuestlineID();
	const FString QuestlineSegment = SanitizeQuestlineTagSegment(
		QuestlineID.IsEmpty() ? QuestlineAsset->GetName() : QuestlineID);
	Segments.Insert(QuestlineSegment, 0);

	// Build: Quest.<QuestlineID>.<QuestLabel>.<StepLabel>
	const FString TagName = TEXT("Quest.") + FString::Join(Segments, TEXT("."));

	UE_LOG(LogSimpleQuest, Verbose, TEXT("ReconstructStepTag: %s → %s"),
		*StepNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString(), *TagName);

	return FGameplayTag::RequestGameplayTag(FName(*TagName), /*bErrorIfNotFound=*/ false);
}

TArray<FString> USimpleQuestEditorUtilities::FindActorNamesWatchingTag(const FGameplayTag& StepTag)
{
	TArray<FString> Names;
	if (!StepTag.IsValid() || !GEditor) return Names;

	for (const FWorldContext& Context : GEditor->GetWorldContexts())
	{
		UWorld* World = Context.World();
		if (!World || Context.WorldType != EWorldType::Editor) continue;

		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (const UQuestTargetComponent* Comp = It->FindComponentByClass<UQuestTargetComponent>())
			{
				if (Comp->GetStepTagsToWatch().HasTagExact(StepTag))
				{
					Names.Add(It->GetActorLabel());
				}
			}
		}
	}

	Names.Sort();
	return Names;
}

