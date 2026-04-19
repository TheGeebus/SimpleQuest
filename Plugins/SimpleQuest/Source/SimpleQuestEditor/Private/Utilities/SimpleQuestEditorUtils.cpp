// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Utilities/SimpleQuestEditorUtils.h"

#include "SimpleQuestLog.h"
#include "K2Nodes/K2Node_CompleteObjectiveWithOutcome.h"
#include "Nodes/QuestlineNode_Exit.h"
#include "Objectives/QuestObjective.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Nodes/QuestlineNode_Step.h"
#include "Nodes/QuestlineNode_Quest.h"
#include "Quests/QuestlineGraph.h"
#include "Components/QuestTargetComponent.h"
#include "Components/QuestGiverComponent.h"
#include "Nodes/Groups/QuestlineNode_ActivationGroupExit.h"
#include "Nodes/Groups/QuestlineNode_ActivationGroupEntry.h"
#include "Quests/QuestNodeBase.h"
#include "Toolkit/QuestlineGraphEditor.h"
#include "Utilities/GroupExaminerTypes.h"
#include "Utilities/QuestlineGraphTraversalPolicy.h"
#include "Toolkit/QuestlineGraphEditor.h"
#include "ToolMenu.h"
#include "ToolMenus.h"


class UQuestlineNode_Exit;


FString FSimpleQuestEditorUtilities::SanitizeQuestlineTagSegment(const FString& InLabel)
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

TArray<FName> FSimpleQuestEditorUtilities::CollectExitOutcomeTagNames(const UEdGraph* Graph)
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

TArray<FGameplayTag> FSimpleQuestEditorUtilities::DiscoverObjectiveOutcomes(TSubclassOf<UQuestObjective> ObjectiveClass)
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

FGameplayTag FSimpleQuestEditorUtilities::ReconstructNodeTagInternal(const UQuestlineNode_ContentBase* ContentNode)
{
	if (!ContentNode) return FGameplayTag();

	const FString NodeLabel = FSimpleQuestEditorUtilities::SanitizeQuestlineTagSegment(
		ContentNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
	if (NodeLabel.IsEmpty()) return FGameplayTag();

	UEdGraph* CurrentGraph = ContentNode->GetGraph();
	if (!CurrentGraph) return FGameplayTag();

	TArray<FString> Segments;
	Segments.Add(NodeLabel);

	UObject* Outer = CurrentGraph->GetOuter();

	while (UQuestlineNode_Quest* QuestNode = Cast<UQuestlineNode_Quest>(Outer))
	{
		const FString QuestLabel = FSimpleQuestEditorUtilities::SanitizeQuestlineTagSegment(
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
	const FString QuestlineSegment = FSimpleQuestEditorUtilities::SanitizeQuestlineTagSegment(
		QuestlineID.IsEmpty() ? QuestlineAsset->GetName() : QuestlineID);
	Segments.Insert(QuestlineSegment, 0);

	const FString TagName = TEXT("Quest.") + FString::Join(Segments, TEXT("."));

	UE_LOG(LogSimpleQuest, Verbose, TEXT("ReconstructNodeTag: %s → %s"),
		*ContentNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString(), *TagName);

	return FGameplayTag::RequestGameplayTag(FName(*TagName), /*bErrorIfNotFound=*/ false);
}

FGameplayTag FSimpleQuestEditorUtilities::ReconstructStepTag(const UQuestlineNode_Step* StepNode)
{
	return ReconstructNodeTagInternal(StepNode);
}

FGameplayTag FSimpleQuestEditorUtilities::ReconstructParentQuestTag(const UQuestlineNode_Step* StepNode)
{
	if (!StepNode) return FGameplayTag();

	UEdGraph* StepGraph = StepNode->GetGraph();
	if (!StepGraph) return FGameplayTag();

	// The step must be inside a quest's inner graph for there to be a parent quest
	UQuestlineNode_Quest* ParentQuest = Cast<UQuestlineNode_Quest>(StepGraph->GetOuter());
	return ReconstructNodeTagInternal(ParentQuest);
}

TArray<FString> FSimpleQuestEditorUtilities::FindActorNamesWatchingTag(const FGameplayTag& StepTag)
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

TArray<FString> FSimpleQuestEditorUtilities::FindActorNamesGivingTag(const FGameplayTag& QuestTag)
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

int32 FSimpleQuestEditorUtilities::ApplyTagRenamesToLoadedWorlds(const TMap<FName, FName>& Renames)
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

FGameplayTag FSimpleQuestEditorUtilities::FindCompiledTagForNode(const UQuestlineNode_Step* StepNode)
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

bool FSimpleQuestEditorUtilities::IsStepTagCurrent(const UQuestlineNode_Step* StepNode)
{
	const FGameplayTag CompiledTag = FindCompiledTagForNode(StepNode);
	if (!CompiledTag.IsValid()) return false;

	const FGameplayTag ReconstructedTag = ReconstructStepTag(StepNode);
	if (!ReconstructedTag.IsValid()) return false;

	return CompiledTag.GetTagName() == ReconstructedTag.GetTagName();
}

void FSimpleQuestEditorUtilities::SyncPinsByCategory(UEdGraphNode* Node, EEdGraphPinDirection Direction, FName PinCategory, const TArray<FName>& DesiredPinNames, const TSet<FName>& InsertBeforeCategories)
{
    if (!Node) return;

    // ----- Collect existing pins of this category (including orphaned) -----

    TArray<UEdGraphPin*> ExistingPins;
    for (UEdGraphPin* Pin : Node->Pins)
    {
        if (Pin->Direction == Direction && Pin->PinType.PinCategory == PinCategory)
        {
            ExistingPins.Add(Pin);
        }
    }

    // ----- Diff against desired set -----

    bool bChanged = false;

    TArray<UEdGraphPin*> PinsToRemove;
    for (UEdGraphPin* Pin : ExistingPins)
    {
        const bool bDesired = DesiredPinNames.Contains(Pin->PinName);
        if (bDesired && Pin->bOrphanedPin)
        {
            Pin->bOrphanedPin = false;
            bChanged = true;
        }
        else if (!bDesired && !Pin->bOrphanedPin)
        {
            if (Pin->LinkedTo.Num() > 0)
            {
                Pin->bOrphanedPin = true;
            }
            else
            {
                PinsToRemove.Add(Pin);
            }
            bChanged = true;
        }
    }

    TArray<FName> NamesToAdd;
    for (const FName& Name : DesiredPinNames)
    {
        const bool bExists = ExistingPins.ContainsByPredicate(
            [&](const UEdGraphPin* Pin) { return Pin->PinName == Name; });
        if (!bExists) NamesToAdd.Add(Name);
    }

    // ----- Apply add/remove -----

    const bool bNeedsReorder = DesiredPinNames.Num() >= 2;
    if (!bChanged && NamesToAdd.IsEmpty() && !bNeedsReorder) return;

    Node->Modify();

    for (UEdGraphPin* Pin : PinsToRemove)
    {
        Node->RemovePin(Pin);
    }

    int32 InsertIndex = INDEX_NONE;
    if (!InsertBeforeCategories.IsEmpty())
    {
        for (int32 i = 0; i < Node->Pins.Num(); i++)
        {
            if (InsertBeforeCategories.Contains(Node->Pins[i]->PinType.PinCategory))
            {
                InsertIndex = i;
                break;
            }
        }
    }

    FEdGraphPinType PinType;
    PinType.PinCategory = PinCategory;

    for (const FName& Name : NamesToAdd)
    {
        Node->CreatePin(Direction, PinType, Name, InsertIndex);
        if (InsertIndex != INDEX_NONE) ++InsertIndex;
    }

    /**
     * ----- Reorder category pins to match DesiredPinNames -----
     *
     * CreatePin appends at the end of the category range (or at InsertIndex), so a toggle-off-then-on cycle lands the new
     * pin at the bottom. Enforce the final order here by rewriting the Pins[] slots occupied by this category: desired pins
     * take the first slots in DesiredPinNames order, orphans fill the remaining slots in their original relative order.
     * Slot positions held by other categories (inputs, other output categories) are untouched.
     */
    TArray<int32> CategorySlotIndices;
    TArray<UEdGraphPin*> CategoryPins;
    for (int32 i = 0; i < Node->Pins.Num(); ++i)
    {
        UEdGraphPin* Pin = Node->Pins[i];
        if (Pin && Pin->Direction == Direction && Pin->PinType.PinCategory == PinCategory)
        {
            CategorySlotIndices.Add(i);
            CategoryPins.Add(Pin);
        }
    }

    if (CategorySlotIndices.Num() >= 2)
    {
        TMap<FName, UEdGraphPin*> ByName;
        for (UEdGraphPin* Pin : CategoryPins)
        {
            ByName.Add(Pin->PinName, Pin);
        }

        TArray<UEdGraphPin*> NewOrder;
        NewOrder.Reserve(CategoryPins.Num());

        // Desired pins in desired order
        for (const FName& Name : DesiredPinNames)
        {
            if (UEdGraphPin** Found = ByName.Find(Name))
            {
                NewOrder.Add(*Found);
                ByName.Remove(Name);
            }
        }
        // Orphans — preserve original relative order
        for (UEdGraphPin* Pin : CategoryPins)
        {
            if (ByName.Contains(Pin->PinName))
            {
                NewOrder.Add(Pin);
                ByName.Remove(Pin->PinName);
            }
        }

        for (int32 i = 0; i < CategorySlotIndices.Num() && i < NewOrder.Num(); ++i)
        {
            if (Node->Pins[CategorySlotIndices[i]] != NewOrder[i])
            {
                Node->Pins[CategorySlotIndices[i]] = NewOrder[i];
                bChanged = true;
            }
        }
    }

    if (UEdGraph* Graph = Node->GetGraph())
    {
        Graph->NotifyGraphChanged();
    }
}

void FSimpleQuestEditorUtilities::SortPinNamesAlphabetical(TArray<FName>& PinNames)
{
	PinNames.Sort([](const FName& A, const FName& B) { return A.Compare(B) < 0; });
}

void FSimpleQuestEditorUtilities::CollectActivationGroupTopology(const FGameplayTag& InGroupTag, FGroupExaminerTopology& OutTopology)
{
	OutTopology.GroupTag = InGroupTag;
	OutTopology.Setters.Empty();
	OutTopology.Getters.Empty();
	if (!InGroupTag.IsValid()) return;

	IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

	TArray<FAssetData> QuestlineAssets;
	AR.GetAssetsByClass(UQuestlineGraph::StaticClass()->GetClassPathName(), QuestlineAssets);

	FQuestlineGraphTraversalPolicy TraversalPolicy;

	for (const FAssetData& AssetData : QuestlineAssets)
	{
		UQuestlineGraph* QuestlineGraph = Cast<UQuestlineGraph>(AssetData.GetAsset()); // sync load
		if (!QuestlineGraph || !QuestlineGraph->QuestlineEdGraph) continue;

		for (UEdGraphNode* Node : QuestlineGraph->QuestlineEdGraph->Nodes)
		{
			/**
			 * Setter: backward-walk each LinkedTo of the Activate input. CollectEffectiveSources expects output-side pins
			 * it can walk through (knots, utility Forward, setter Forward, getter tag dereference). Transitive group-chain
			 * sources are naturally captured — a getter-dereference chain surfaces the ultimate content-node sources.
			 */
			if (UQuestlineNode_ActivationGroupEntry* Setter = Cast<UQuestlineNode_ActivationGroupEntry>(Node))
			{
				if (Setter->GetGroupTag() != InGroupTag) continue;

				FGroupExaminerEndpoint Endpoint;
				Endpoint.Node = Setter;
				Endpoint.Asset = QuestlineGraph;

				if (UEdGraphPin* ActivatePin = Setter->FindPin(TEXT("Activate"), EGPD_Input))
				{
					TSet<const UEdGraphPin*> SourcePins;
					TSet<const UEdGraphNode*> VisitedNodes;
					for (const UEdGraphPin* Linked : ActivatePin->LinkedTo)
					{
						TraversalPolicy.CollectEffectiveSources(Linked, SourcePins, VisitedNodes);
					}

					for (const UEdGraphPin* SourcePin : SourcePins)
					{
						if (!SourcePin) continue;
						FGroupExaminerReference Ref;
						Ref.Node = SourcePin->GetOwningNode();
						Ref.Asset = QuestlineGraph; // walker stays within-graph — containing asset is the current one
						Ref.PinLabel = SourcePin->PinName.ToString();
						Endpoint.References.Add(Ref);
					}
				}

				OutTopology.Setters.Add(Endpoint);
				continue;
			}

			/**
			 * Getter: forward-walk from the Forward output. CollectActivationTerminals already iterates LinkedTo internally
			 * and terminates at content/exit Activate or Deactivate pins, so direct invocation on the Forward pin is correct.
			 */
			if (UQuestlineNode_ActivationGroupExit* Getter = Cast<UQuestlineNode_ActivationGroupExit>(Node))
			{
				if (Getter->GetGroupTag() != InGroupTag) continue;

				FGroupExaminerEndpoint Endpoint;
				Endpoint.Node = Getter;
				Endpoint.Asset = QuestlineGraph;

				if (UEdGraphPin* ForwardPin = Getter->FindPin(TEXT("Forward"), EGPD_Output))
				{
					TSet<const UEdGraphPin*> Terminals;
					TSet<const UEdGraphNode*> VisitedNodes;
					TraversalPolicy.CollectActivationTerminals(ForwardPin, Terminals, VisitedNodes);

					for (const UEdGraphPin* Terminal : Terminals)
					{
						if (!Terminal) continue;
						FGroupExaminerReference Ref;
						Ref.Node = Terminal->GetOwningNode();
						Ref.Asset = QuestlineGraph;
						Ref.PinLabel = Terminal->PinName.ToString();
						Endpoint.References.Add(Ref);
					}
				}

				OutTopology.Getters.Add(Endpoint);
				continue;
			}
		}
	}
}

void FSimpleQuestEditorUtilities::NavigateToEdGraphNode(const UEdGraphNode* Node)
{
	if (!Node || !Node->GetGraph() || !GEditor) return;

	UQuestlineGraph* QuestlineGraph = nullptr;
	for (UObject* Outer = Node->GetGraph(); Outer; Outer = Outer->GetOuter())
	{
		QuestlineGraph = Cast<UQuestlineGraph>(Outer);
		if (QuestlineGraph) break;
	}
	if (!QuestlineGraph) return;

	UAssetEditorSubsystem* EditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	EditorSubsystem->OpenEditorForAsset(QuestlineGraph);

	if (IAssetEditorInstance* EditorInstance = EditorSubsystem->FindEditorForAsset(QuestlineGraph, false))
	{
		static_cast<FQuestlineGraphEditor*>(EditorInstance)->NavigateToLocation(Node->GetGraph(), const_cast<UEdGraphNode*>(Node));
	}
}

FQuestlineGraphEditor* FSimpleQuestEditorUtilities::GetEditorForNode(const UEdGraphNode* Node)
{
	if (!Node || !Node->GetGraph() || !GEditor) return nullptr;

	UQuestlineGraph* QuestlineGraph = nullptr;
	for (UObject* Outer = Node->GetGraph(); Outer; Outer = Outer->GetOuter())
	{
		QuestlineGraph = Cast<UQuestlineGraph>(Outer);
		if (QuestlineGraph) break;
	}
	if (!QuestlineGraph) return nullptr;

	UAssetEditorSubsystem* EditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	if (!EditorSubsystem) return nullptr;

	IAssetEditorInstance* EditorInstance = EditorSubsystem->FindEditorForAsset(QuestlineGraph, /*bFocusIfOpen=*/ false);
	if (!EditorInstance) return nullptr;

	return static_cast<FQuestlineGraphEditor*>(EditorInstance);
}

void FSimpleQuestEditorUtilities::AddExamineGroupConnectionsEntry(FToolMenuSection& Section, UEdGraphNode* Node, FGameplayTag GroupTag)
{
	Section.AddMenuEntry(
		TEXT("ExamineGroupConnections"),
		NSLOCTEXT("SimpleQuestEditor", "ExamineGroupConnections_Label", "Examine Group Connections"),
		NSLOCTEXT("SimpleQuestEditor", "ExamineGroupConnections_Tooltip",
			"Open the Group Examiner panel and pin this group — shows all setters, getters, and their connections across the project. Disabled when this node has no group tag set."),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([WeakNode = TWeakObjectPtr<UEdGraphNode>(Node), GroupTag]()
			{
				UEdGraphNode* PinnedNode = WeakNode.Get();
				if (!PinnedNode) return;
				if (FQuestlineGraphEditor* Editor = FSimpleQuestEditorUtilities::GetEditorForNode(PinnedNode))
				{
					Editor->PinGroupExaminer(GroupTag, PinnedNode);
				}
			}),
			FCanExecuteAction::CreateLambda([GroupTag]()
			{
				return GroupTag.IsValid();
			})
		)
	);
}

