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
#include "Components/QuestGiverComponent.h"
#include "Quests/QuestNodeBase.h"


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
		// Deterministic pin order regardless of discovery source — prevents pin shuffling across rebuilds
		AllOutcomes.Sort([](const FGameplayTag& A, const FGameplayTag& B)
		{
			return A.GetTagName().LexicalLess(B.GetTagName());
		});
		
		UE_LOG(LogSimpleQuest, Verbose,	TEXT("DiscoverObjectiveOutcomes: Found %d outcome(s) for %s"),	AllOutcomes.Num(), *ObjectiveClass->GetName());
	}

	return AllOutcomes;
}

namespace
{
	/** Walks the graph outer chain from any content node to reconstruct its compiled tag. */
	FGameplayTag ReconstructNodeTagInternal(const UQuestlineNode_ContentBase* ContentNode)
	{
		if (!ContentNode) return FGameplayTag();

		const FString NodeLabel = USimpleQuestEditorUtilities::SanitizeQuestlineTagSegment(
			ContentNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
		if (NodeLabel.IsEmpty()) return FGameplayTag();

		UEdGraph* CurrentGraph = ContentNode->GetGraph();
		if (!CurrentGraph) return FGameplayTag();

		TArray<FString> Segments;
		Segments.Add(NodeLabel);

		UObject* Outer = CurrentGraph->GetOuter();

		while (UQuestlineNode_Quest* QuestNode = Cast<UQuestlineNode_Quest>(Outer))
		{
			const FString QuestLabel = USimpleQuestEditorUtilities::SanitizeQuestlineTagSegment(
				QuestNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
			if (QuestLabel.IsEmpty()) return FGameplayTag();

			Segments.Insert(QuestLabel, 0);

			UEdGraph* QuestGraph = QuestNode->GetGraph();
			if (!QuestGraph) return FGameplayTag();
			Outer = QuestGraph->GetOuter();
		}

		const UQuestlineGraph* QuestlineAsset = Cast<UQuestlineGraph>(Outer);
		if (!QuestlineAsset) return FGameplayTag();

		const FString& QuestlineID = QuestlineAsset->GetQuestlineID();
		const FString QuestlineSegment = USimpleQuestEditorUtilities::SanitizeQuestlineTagSegment(
			QuestlineID.IsEmpty() ? QuestlineAsset->GetName() : QuestlineID);
		Segments.Insert(QuestlineSegment, 0);

		const FString TagName = TEXT("Quest.") + FString::Join(Segments, TEXT("."));

		UE_LOG(LogSimpleQuest, Verbose, TEXT("ReconstructNodeTag: %s → %s"),
			*ContentNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString(), *TagName);

		return FGameplayTag::RequestGameplayTag(FName(*TagName), /*bErrorIfNotFound=*/ false);
	}
}

FGameplayTag USimpleQuestEditorUtilities::ReconstructStepTag(const UQuestlineNode_Step* StepNode)
{
	return ReconstructNodeTagInternal(StepNode);
}

FGameplayTag USimpleQuestEditorUtilities::ReconstructParentQuestTag(const UQuestlineNode_Step* StepNode)
{
	if (!StepNode) return FGameplayTag();

	UEdGraph* StepGraph = StepNode->GetGraph();
	if (!StepGraph) return FGameplayTag();

	// The step must be inside a quest's inner graph for there to be a parent quest
	UQuestlineNode_Quest* ParentQuest = Cast<UQuestlineNode_Quest>(StepGraph->GetOuter());
	return ReconstructNodeTagInternal(ParentQuest);
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

TArray<FString> USimpleQuestEditorUtilities::FindActorNamesGivingTag(const FGameplayTag& QuestTag)
{
	TArray<FString> Names;
	if (!QuestTag.IsValid() || !GEditor) return Names;

	for (const FWorldContext& Context : GEditor->GetWorldContexts())
	{
		UWorld* World = Context.World();
		if (!World || Context.WorldType != EWorldType::Editor) continue;

		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (const UQuestGiverComponent* Comp = It->FindComponentByClass<UQuestGiverComponent>())
			{
				if (Comp->GetQuestTagsToGive().HasTagExact(QuestTag))
				{
					Names.Add(It->GetActorLabel());
				}
			}
		}
	}

	Names.Sort();
	return Names;
}

int32 USimpleQuestEditorUtilities::ApplyTagRenamesToLoadedWorlds(const TMap<FName, FName>& Renames)
{
	if (Renames.Num() == 0 || !GEditor) return 0;

	int32 ModifiedActors = 0;

	for (const FWorldContext& Context : GEditor->GetWorldContexts())
	{
		UWorld* World = Context.World();
		if (!World || Context.WorldType != EWorldType::Editor) continue;

		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			bool bActorModified = false;

			TInlineComponentArray<UQuestComponentBase*> QuestComps;
			Actor->GetComponents(QuestComps);

			for (UQuestComponentBase* Comp : QuestComps)
			{
				const int32 SwapCount = Comp->ApplyTagRenames(Renames);
				if (SwapCount > 0)
				{
					Comp->Modify();
					bActorModified = true;
					UE_LOG(LogSimpleQuest, Log, TEXT("  Tag rename: %s on '%s' — %d tag(s) updated"),
						*Comp->GetClass()->GetName(), *Actor->GetActorLabel(), SwapCount);
				}
			}

			if (bActorModified)
			{
				Actor->MarkPackageDirty();
				ModifiedActors++;
			}
		}
	}

	return ModifiedActors;
}

FGameplayTag USimpleQuestEditorUtilities::FindCompiledTagForNode(const UQuestlineNode_Step* StepNode)
{
	if (!StepNode) return FGameplayTag();

	UEdGraph* Graph = StepNode->GetGraph();
	if (!Graph) return FGameplayTag();

	UObject* Outer = Graph->GetOuter();
	while (UQuestlineNode_Quest* QuestNode = Cast<UQuestlineNode_Quest>(Outer))
	{
		UEdGraph* QuestGraph = QuestNode->GetGraph();
		if (!QuestGraph) return FGameplayTag();
		Outer = QuestGraph->GetOuter();
	}

	const UQuestlineGraph* QuestlineAsset = Cast<UQuestlineGraph>(Outer);
	if (!QuestlineAsset) return FGameplayTag();

	for (const auto& [TagName, NodeInstance] : QuestlineAsset->GetCompiledNodes())
	{
		if (NodeInstance && NodeInstance->GetQuestGuid() == StepNode->QuestGuid)
		{
			return FGameplayTag::RequestGameplayTag(TagName, false);
		}
	}

	return FGameplayTag();
}

bool USimpleQuestEditorUtilities::IsStepTagCurrent(const UQuestlineNode_Step* StepNode)
{
	const FGameplayTag CompiledTag = FindCompiledTagForNode(StepNode);
	if (!CompiledTag.IsValid()) return false;

	const FGameplayTag ReconstructedTag = ReconstructStepTag(StepNode);
	if (!ReconstructedTag.IsValid()) return false;

	return CompiledTag.GetTagName() == ReconstructedTag.GetTagName();
}

