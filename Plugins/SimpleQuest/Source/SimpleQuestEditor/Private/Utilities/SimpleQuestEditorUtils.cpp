// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Utilities/SimpleQuestEditorUtils.h"

#include "SimpleQuestLog.h"
#include "K2Nodes/K2Node_CompleteObjectiveWithOutcome.h"
#include "Nodes/QuestlineNode_Exit.h"
#include "Objectives/QuestObjective.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "GameplayTagsManager.h"
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
#include "Nodes/QuestlineNode_Knot.h"
#include "Nodes/Groups/QuestlineNode_PrerequisiteRuleEntry.h"
#include "Nodes/Groups/QuestlineNode_PrerequisiteRuleExit.h"
#include "Nodes/Prerequisites/QuestlineNode_PrerequisiteAnd.h"
#include "Nodes/Prerequisites/QuestlineNode_PrerequisiteNot.h"
#include "Nodes/Prerequisites/QuestlineNode_PrerequisiteOr.h"
#include "Types/PrereqExaminerTypes.h"
#include "Types/QuestPinRole.h"
#include "Utilities/QuestStateTagUtils.h"


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

FGameplayTag FSimpleQuestEditorUtilities::FindCompiledTagForNode(const UQuestlineNode_ContentBase* ContentNode)
{
	if (!ContentNode)
	{
		return FGameplayTag();
	}

	UEdGraph* Graph = ContentNode->GetGraph();
	if (!Graph)
	{
		return FGameplayTag();
	}

	UObject* Outer = Graph->GetOuter();
	while (UQuestlineNode_Quest* QuestNode = Cast<UQuestlineNode_Quest>(Outer))
	{
		UEdGraph* QuestGraph = QuestNode->GetGraph();
		if (!QuestGraph)
		{
			return FGameplayTag();
		}
		Outer = QuestGraph->GetOuter();
	}

	const UQuestlineGraph* QuestlineAsset = Cast<UQuestlineGraph>(Outer);
	if (!QuestlineAsset)
	{
		UE_LOG(LogSimpleQuest, Warning, TEXT("FindCompiledTagForNode: Node '%s' — Outer chain did not terminate at a UQuestlineGraph (final Outer class=%s)"),
			*ContentNode->NodeLabel.ToString(), Outer ? *Outer->GetClass()->GetName() : TEXT("null"));
		return FGameplayTag();
	}

	const auto& CompiledNodes = QuestlineAsset->GetCompiledNodes();
	UE_LOG(LogSimpleQuest, VeryVerbose, TEXT("FindCompiledTagForNode: Searching for Node '%s' QuestGuid=%s in %s's %d compiled nodes"),
		*ContentNode->NodeLabel.ToString(), *ContentNode->QuestGuid.ToString(), *QuestlineAsset->GetName(), CompiledNodes.Num());

	for (const auto& [TagName, NodeInstance] : CompiledNodes)
	{
		if (!NodeInstance)
		{
			continue;
		}
		UE_LOG(LogSimpleQuest, VeryVerbose, TEXT("  Slot '%s': instance QuestGuid=%s class=%s"),
			*TagName.ToString(), *NodeInstance->GetQuestGuid().ToString(), *NodeInstance->GetClass()->GetName());
		if (NodeInstance->GetQuestGuid() == ContentNode->QuestGuid)
		{
			return FGameplayTag::RequestGameplayTag(TagName, false);
		}
	}

	UE_LOG(LogSimpleQuest, Verbose, TEXT("FindCompiledTagForNode: Node '%s' QuestGuid=%s — no matching compiled instance"),
		*ContentNode->NodeLabel.ToString(), *ContentNode->QuestGuid.ToString());
	return FGameplayTag();
}

FGameplayTag FSimpleQuestEditorUtilities::ResolveLeafFactForOutputPin(const UEdGraphPin* OutputPin, FGameplayTag& OutSourceTag)
{
	OutSourceTag = FGameplayTag();
	if (!OutputPin) return FGameplayTag();

	UEdGraphNode* OwningNode = OutputPin->GetOwningNode();
	const UQuestlineNode_ContentBase* ContentNode = Cast<UQuestlineNode_ContentBase>(OwningNode);
	if (!ContentNode) return FGameplayTag(); // Entry/Rule nodes / combinators not covered in Session B's MVP.

	// Walk Outer chain to the containing UQuestlineGraph, then look up the source node's compiled runtime tag via
	// QuestGuid — same resolution path used by FQuestPIEDebugChannel::ResolveRuntimeTag and FindCompiledTagForNode.
	UObject* Outer = OwningNode->GetGraph() ? OwningNode->GetGraph()->GetOuter() : nullptr;
	while (Outer && !Outer->IsA<UQuestlineGraph>()) Outer = Outer->GetOuter();
	const UQuestlineGraph* QuestlineAsset = Cast<UQuestlineGraph>(Outer);
	if (!QuestlineAsset) return FGameplayTag();

	FName SourceTagName = NAME_None;
	for (const auto& [TagName, NodeInstance] : QuestlineAsset->GetCompiledNodes())
	{
		if (NodeInstance && NodeInstance->GetQuestGuid() == ContentNode->QuestGuid)
		{
			SourceTagName = TagName;
			break;
		}
	}
	if (SourceTagName.IsNone()) return FGameplayTag();

	OutSourceTag = FGameplayTag::RequestGameplayTag(SourceTagName, /*ErrorIfNotFound*/ false);

	// Build the leaf fact per pin role — matches FQuestlineGraphCompiler::CompilePrerequisiteFromOutputPin content-node
	// branch. AnyOutcome → QuestState.<src>.Completed (source done, regardless of outcome). Named outcome →
	// QuestState.<src>.Outcome.<leaf>.
	const EQuestPinRole Role = UQuestlineNodeBase::GetPinRoleOf(OutputPin);
	if (Role == EQuestPinRole::AnyOutcomeOut)
	{
		const FName FactName = FQuestStateTagUtils::MakeStateFact(SourceTagName, FQuestStateTagUtils::Leaf_Completed);
		return FGameplayTag::RequestGameplayTag(FactName, false);
	}

	const FGameplayTag OutcomeTag = UGameplayTagsManager::Get().RequestGameplayTag(OutputPin->PinName, false);
	if (!OutcomeTag.IsValid()) return FGameplayTag();
	const FName FactName = FQuestStateTagUtils::MakeNodeOutcomeFact(SourceTagName, OutcomeTag);
	return FGameplayTag::RequestGameplayTag(FactName, false);
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

				UEdGraphPin* ActivatePin = Setter->GetPinByRole(EQuestPinRole::ExecIn);
				if (!ActivatePin)
				{
					UE_LOG(LogSimpleQuest, Warning, TEXT("[GroupExaminer] Activation Group Entry '%s' in '%s' has no ExecIn role pin — topology will list zero sources."),
						*Setter->GetNodeTitle(ENodeTitleType::ListView).ToString(), *QuestlineGraph->GetName());
				}
				else
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

				UEdGraphPin* ForwardPin = Getter->GetPinByRole(EQuestPinRole::ExecForwardOut);
				if (!ForwardPin)
				{
					UE_LOG(LogSimpleQuest, Warning, TEXT("[GroupExaminer] Activation Group Exit '%s' in '%s' has no ExecForwardOut role pin — topology will list zero sources."),
						*Getter->GetNodeTitle(ENodeTitleType::ListView).ToString(), *QuestlineGraph->GetName());
				}
				else
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

namespace PrereqExaminer_Internal
{
    /** Finds the Prerequisite Rule Entry in Graph whose GroupTag matches RuleTag, or nullptr. */
    UQuestlineNode_PrerequisiteRuleEntry* FindRuleEntryInGraph(const UEdGraph* Graph, const FGameplayTag& RuleTag)
    {
        if (!Graph || !RuleTag.IsValid()) return nullptr;
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (UQuestlineNode_PrerequisiteRuleEntry* Entry = Cast<UQuestlineNode_PrerequisiteRuleEntry>(Node))
            {
                if (Entry->GroupTag == RuleTag) return Entry;
            }
        }
        return nullptr;
    }

    /**
     * Project-wide lookup for the Rule Entry defining a given tag. Local graph first (common case); AR scan + sync-load
     * fallback for cross-asset references. Relies on the compile-time duplicate-tag detection pass to guarantee one
     * authoritative Entry per tag at compile; if two exist at authoring time this returns the first match.
     */
    UQuestlineNode_PrerequisiteRuleEntry* ResolveRuleEntry(const UEdGraph* LocalGraph, const FGameplayTag& RuleTag)
    {
        if (!RuleTag.IsValid()) return nullptr;

        if (UQuestlineNode_PrerequisiteRuleEntry* Local = FindRuleEntryInGraph(LocalGraph, RuleTag)) return Local;

        const FAssetRegistryModule& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
        TArray<FAssetData> Assets;
        AR.Get().GetAssetsByClass(UQuestlineGraph::StaticClass()->GetClassPathName(), Assets);
        for (const FAssetData& Data : Assets)
        {
            UQuestlineGraph* Loaded = Cast<UQuestlineGraph>(Data.GetAsset());
            if (!Loaded || Loaded->QuestlineEdGraph == LocalGraph) continue;
            if (UQuestlineNode_PrerequisiteRuleEntry* Found = FindRuleEntryInGraph(Loaded->QuestlineEdGraph, RuleTag))
            {
                return Found;
            }
        }
        return nullptr;
    }

    /** Forward declaration — recursion inside the namespace. */
    int32 WalkFromOutputPin(const UEdGraphPin* OutputPin, FPrereqExaminerTree& Tree, TSet<const UEdGraphNode*>& RuleEntriesVisited);

    /** Adds a combinator node (And/Or/Not) and recursively walks its PrereqIn pins into ChildIndices. */
    int32 EmitCombinator(UEdGraphNode* CombinatorNode, EPrereqExaminerNodeType Type, const FText& Label,
        FPrereqExaminerTree& Tree, TSet<const UEdGraphNode*>& RuleEntriesVisited)
    {
        FPrereqExaminerNode NewNode;
        NewNode.Type = Type;
        NewNode.DisplayLabel = Label;
        NewNode.SourceNode = CombinatorNode;
        const int32 Index = Tree.Nodes.Add(NewNode);

        if (const UQuestlineNodeBase* Base = Cast<UQuestlineNodeBase>(CombinatorNode))
        {
            TArray<UEdGraphPin*> Inputs;
            Base->GetPinsByRole(EQuestPinRole::PrereqIn, Inputs);
            for (UEdGraphPin* InPin : Inputs)
            {
                if (!InPin || InPin->LinkedTo.Num() == 0) continue;
                const int32 ChildIdx = WalkFromOutputPin(InPin->LinkedTo[0], Tree, RuleEntriesVisited);
                if (ChildIdx != INDEX_NONE) Tree.Nodes[Index].ChildIndices.Add(ChildIdx);
            }
        }
        return Index;
    }

    int32 WalkFromOutputPin(const UEdGraphPin* OutputPin, FPrereqExaminerTree& Tree, TSet<const UEdGraphNode*>& RuleEntriesVisited)
    {
        if (!OutputPin) return INDEX_NONE;
        UEdGraphNode* OwningNode = OutputPin->GetOwningNode();
        if (!OwningNode) return INDEX_NONE;

        // Knots: transparent passthrough — walk through KnotIn's upstream.
        if (UQuestlineNode_Knot* Knot = Cast<UQuestlineNode_Knot>(OwningNode))
        {
            if (UEdGraphPin* KnotIn = Knot->FindPin(TEXT("KnotIn"), EGPD_Input))
            {
                if (KnotIn->LinkedTo.Num() > 0) return WalkFromOutputPin(KnotIn->LinkedTo[0], Tree, RuleEntriesVisited);
            }
            return INDEX_NONE;
        }

        // Combinators.
        if (Cast<UQuestlineNode_PrerequisiteAnd>(OwningNode))
        {
            return EmitCombinator(OwningNode, EPrereqExaminerNodeType::And,
                NSLOCTEXT("SimpleQuestEditor", "PrereqExaminerAnd", "AND"), Tree, RuleEntriesVisited);
        }
        if (Cast<UQuestlineNode_PrerequisiteOr>(OwningNode))
        {
            return EmitCombinator(OwningNode, EPrereqExaminerNodeType::Or,
                NSLOCTEXT("SimpleQuestEditor", "PrereqExaminerOr", "OR"), Tree, RuleEntriesVisited);
        }
        if (Cast<UQuestlineNode_PrerequisiteNot>(OwningNode))
        {
            return EmitCombinator(OwningNode, EPrereqExaminerNodeType::Not,
                NSLOCTEXT("SimpleQuestEditor", "PrereqExaminerNot", "NOT"), Tree, RuleEntriesVisited);
        }

        // Rule Entry Forward: direct-eval — inline the Entry's Enter expression (no RuleRef emitted).
        if (UQuestlineNode_PrerequisiteRuleEntry* Entry = Cast<UQuestlineNode_PrerequisiteRuleEntry>(OwningNode))
        {
            if (UEdGraphPin* EnterPin = Entry->GetPinByRole(EQuestPinRole::PrereqIn))
            {
                if (EnterPin->LinkedTo.Num() > 0) return WalkFromOutputPin(EnterPin->LinkedTo[0], Tree, RuleEntriesVisited);
            }
            return INDEX_NONE;
        }

        // Rule Exit: emit a RuleRef, drill into the defining Entry's Enter expression.
        if (UQuestlineNode_PrerequisiteRuleExit* Exit = Cast<UQuestlineNode_PrerequisiteRuleExit>(OwningNode))
        {
            FPrereqExaminerNode Ref;
            Ref.Type = EPrereqExaminerNodeType::RuleRef;
            Ref.LeafTag = Exit->GroupTag;
            Ref.SourceNode = Exit;
            const FString RuleName = Exit->GroupTag.IsValid() ? Exit->GroupTag.GetTagName().ToString() : TEXT("(unset)");
            Ref.DisplayLabel = FText::FromString(FString::Printf(TEXT("Rule: %s"), *RuleName));

            UQuestlineNode_PrerequisiteRuleEntry* DefiningEntry =
                ResolveRuleEntry(OwningNode->GetGraph(), Exit->GroupTag);
            Ref.RuleEntryNode = DefiningEntry;

            const int32 Index = Tree.Nodes.Add(Ref);

            // Cycle-guarded eager drill-down into the rule's Enter expression.
            if (DefiningEntry && !RuleEntriesVisited.Contains(DefiningEntry))
            {
                RuleEntriesVisited.Add(DefiningEntry);
                if (UEdGraphPin* EnterPin = DefiningEntry->GetPinByRole(EQuestPinRole::PrereqIn))
                {
                    if (EnterPin->LinkedTo.Num() > 0)
                    {
                        const int32 ChildIdx = WalkFromOutputPin(EnterPin->LinkedTo[0], Tree, RuleEntriesVisited);
                        if (ChildIdx != INDEX_NONE) Tree.Nodes[Index].ChildIndices.Add(ChildIdx);
                    }
                }
            }
            return Index;
        }

    	// Everything else (content nodes, Start terminal, etc.) compiles to a Leaf under the compiler's semantics.
    	FPrereqExaminerNode Leaf;
    	Leaf.Type = EPrereqExaminerNodeType::Leaf;
    	Leaf.SourceNode = OwningNode;

    	// Populate the compiler-equivalent fact tag + source runtime tag so the panel can query PIE state per leaf.
    	// Both stay invalid for node types the helper doesn't cover (Entry outcome leaves, etc.) — the panel then
    	// renders neutral for those leaves rather than a misleading "NotStarted" grey.
    	Leaf.LeafTag = FSimpleQuestEditorUtilities::ResolveLeafFactForOutputPin(OutputPin, Leaf.LeafSourceTag);

    	// Split source + outcome labels so the leaf widget can render each on its own row with a bold header.
    	Leaf.LeafSourceLabel = FText::FromString(OwningNode->GetNodeTitle(ENodeTitleType::ListView).ToString());
    	const EQuestPinRole Role = UQuestlineNodeBase::GetPinRoleOf(OutputPin);
    	if (Role == EQuestPinRole::AnyOutcomeOut)
    	{
    		Leaf.LeafOutcomeLabel = FText::FromString(TEXT("Any Outcome"));
    	}
    	else
    	{
    		// Tag-picker syntax: strip the Quest.Outcome. root (present on all named outcome pins), then split the remainder
    		// into category prefix + leaf so the widget can render the hierarchy deemphasized above the leaf segment.
    		static const FString OutcomePrefix = TEXT("Quest.Outcome.");
    		FString Remainder = OutputPin->PinName.ToString();
    		if (Remainder.StartsWith(OutcomePrefix)) Remainder = Remainder.RightChop(OutcomePrefix.Len());

    		int32 LastDot = INDEX_NONE;
    		if (Remainder.FindLastChar(TEXT('.'), LastDot))
    		{
    			Leaf.LeafOutcomeCategory = FText::FromString(Remainder.Left(LastDot + 1)); // includes trailing dot
    			Leaf.LeafOutcomeLabel    = FText::FromString(Remainder.Mid(LastDot + 1));
    		}
    		else
    		{
    			Leaf.LeafOutcomeLabel = FText::FromString(Remainder);
    		}
    	}

    	// DisplayLabel retained for any legacy consumers (currently none for Leaf — pills filter by type — but kept
    	// populated for diagnostics / future reuse).
    	Leaf.DisplayLabel = FText::FromString(FString::Printf(TEXT("%s: %s%s"), *Leaf.LeafSourceLabel.ToString(), *Leaf.LeafOutcomeCategory.ToString(), *Leaf.LeafOutcomeLabel.ToString()));

    	return Tree.Nodes.Add(Leaf);
    }
}

FPrereqExaminerTree FSimpleQuestEditorUtilities::CollectPrereqExpressionTopology(UEdGraphNode* ContextNode)
{
	
    FPrereqExaminerTree Tree;
    Tree.ContextNode = ContextNode;
    if (!ContextNode) return Tree;

    using namespace PrereqExaminer_Internal;
    TSet<const UEdGraphNode*> RuleEntriesVisited;

	// Rule Entry: header populated; walk its own Enter expression.
	if (UQuestlineNode_PrerequisiteRuleEntry* Entry = Cast<UQuestlineNode_PrerequisiteRuleEntry>(ContextNode))
	{
		Tree.RuleTag = Entry->GroupTag;
		Tree.RuleEntryNode = Entry;
		RuleEntriesVisited.Add(Entry);

		UEdGraphPin* EnterPin = Entry->GetPinByRole(EQuestPinRole::PrereqIn);
		if (EnterPin && EnterPin->LinkedTo.Num() > 0)
		{
			Tree.RootIndex = WalkFromOutputPin(EnterPin->LinkedTo[0], Tree, RuleEntriesVisited);
		}
		return Tree;
	}

    // Rule Exit: header populated; resolve to the defining Entry, walk ITS Enter expression.
    if (UQuestlineNode_PrerequisiteRuleExit* Exit = Cast<UQuestlineNode_PrerequisiteRuleExit>(ContextNode))
    {
        Tree.RuleTag = Exit->GroupTag;
        if (UQuestlineNode_PrerequisiteRuleEntry* Defining = ResolveRuleEntry(ContextNode->GetGraph(), Exit->GroupTag))
        {
            Tree.RuleEntryNode = Defining;
            RuleEntriesVisited.Add(Defining);
            if (UEdGraphPin* EnterPin = Defining->GetPinByRole(EQuestPinRole::PrereqIn))
            {
                if (EnterPin->LinkedTo.Num() > 0)
                    Tree.RootIndex = WalkFromOutputPin(EnterPin->LinkedTo[0], Tree, RuleEntriesVisited);
            }
        }
        return Tree;
    }

    // Combinator context: emit the combinator itself as root, walking its inputs.
    if (Cast<UQuestlineNode_PrerequisiteAnd>(ContextNode))
    {
        Tree.RootIndex = EmitCombinator(ContextNode, EPrereqExaminerNodeType::And,
            NSLOCTEXT("SimpleQuestEditor", "PrereqExaminerAnd", "AND"), Tree, RuleEntriesVisited);
        return Tree;
    }
    if (Cast<UQuestlineNode_PrerequisiteOr>(ContextNode))
    {
        Tree.RootIndex = EmitCombinator(ContextNode, EPrereqExaminerNodeType::Or,
            NSLOCTEXT("SimpleQuestEditor", "PrereqExaminerOr", "OR"), Tree, RuleEntriesVisited);
        return Tree;
    }
    if (Cast<UQuestlineNode_PrerequisiteNot>(ContextNode))
    {
        Tree.RootIndex = EmitCombinator(ContextNode, EPrereqExaminerNodeType::Not,
            NSLOCTEXT("SimpleQuestEditor", "PrereqExaminerNot", "NOT"), Tree, RuleEntriesVisited);
        return Tree;
    }

    // Content node context: walk the Prerequisites input.
	if (UQuestlineNodeBase* Base = Cast<UQuestlineNodeBase>(ContextNode))
	{
		if (UEdGraphPin* PrereqPin = Base->GetPinByRole(EQuestPinRole::PrereqIn))
		{
			if (PrereqPin->LinkedTo.Num() > 0) Tree.RootIndex = WalkFromOutputPin(PrereqPin->LinkedTo[0], Tree, RuleEntriesVisited);
		}
	}
	return Tree;
}

void FSimpleQuestEditorUtilities::AddExaminePrereqExpressionEntry(FToolMenuSection& Section, UEdGraphNode* ContextNode)
{
    Section.AddMenuEntry(
        TEXT("ExaminePrereqExpression"),
        NSLOCTEXT("SimpleQuestEditor", "ExaminePrereqExpression_Label", "Examine Prerequisite Expression"),
        NSLOCTEXT("SimpleQuestEditor", "ExaminePrereqExpression_Tooltip",
            "Pin this node's prerequisite expression to the Prerequisite Examiner panel for real-time inspection as you edit."),
        FSlateIcon(),
        FUIAction(FExecuteAction::CreateLambda([ContextNode]()
        {
            if (FQuestlineGraphEditor* Editor = GetEditorForNode(ContextNode))
            {
                Editor->PinPrereqExaminer(ContextNode);
            }
        }))
    );
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

