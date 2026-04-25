// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Utilities/SimpleQuestEditorUtils.h"

#include "SimpleQuestLog.h"
#include "K2Nodes/K2Node_CompleteObjectiveWithOutcome.h"
#include "Nodes/QuestlineNode_Exit.h"
#include "Objectives/QuestObjective.h"
#include "Editor.h"
#include "EditorWorldUtils.h"
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
#include "Components/QuestWatcherComponent.h"
#include "Engine/InheritableComponentHandler.h"
#include "Engine/SCS_Node.h"
#include "Nodes/QuestlineNode_Knot.h"
#include "Nodes/Groups/QuestlineNode_PrerequisiteRuleEntry.h"
#include "Nodes/Groups/QuestlineNode_PrerequisiteRuleExit.h"
#include "Nodes/Prerequisites/QuestlineNode_PrerequisiteAnd.h"
#include "Nodes/Prerequisites/QuestlineNode_PrerequisiteNot.h"
#include "Nodes/Prerequisites/QuestlineNode_PrerequisiteOr.h"
#include "Types/PrereqExaminerTypes.h"
#include "Types/QuestPinRole.h"
#include "Utilities/QuestStateTagUtils.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionActorDescInstance.h"
#include "WorldPartition/WorldPartitionHelpers.h"


namespace
{
    /** (ContextualTag, ParentAssetDisplayName) pairs — shared output of the AR walk for a single editor node. */
    struct FQuestContextualTagMatch
    {
        FGameplayTag Tag;
        FText ParentAssetDisplayName;
    };

    /**
     * Scans the Asset Registry for questline assets (other than this node's home) whose CompiledQuestTags contain an
     * entry ending with this node's relative path. Returns one match per compiled-tag hit, with the parent asset's
     * display name (FriendlyName override if present, otherwise asset name). Used by both the tag-only and
     * actor-entry public accessors below.
     */
    TArray<FQuestContextualTagMatch> CollectContextualTagMatchesForEditorNode(const UQuestlineNode_ContentBase* ContentNode)
    {
        TArray<FQuestContextualTagMatch> Result;
        if (!ContentNode) return Result;

        const FGameplayTag CompiledTag = FSimpleQuestEditorUtilities::FindCompiledTagForNode(ContentNode);
        if (!CompiledTag.IsValid()) return Result;

        UEdGraph* Graph = ContentNode->GetGraph();
        if (!Graph) return Result;

        UObject* Outer = Graph->GetOuter();
        while (UQuestlineNode_Quest* QuestNode = Cast<UQuestlineNode_Quest>(Outer))
        {
            UEdGraph* QuestGraph = QuestNode->GetGraph();
            if (!QuestGraph) return Result;
            Outer = QuestGraph->GetOuter();
        }
        const UQuestlineGraph* HomeAsset = Cast<UQuestlineGraph>(Outer);
        if (!HomeAsset) return Result;

        const FName HomePackageName = HomeAsset->GetOutermost()->GetFName();
        const FString HomeID = HomeAsset->GetQuestlineID().IsEmpty() ? HomeAsset->GetName() : HomeAsset->GetQuestlineID();
        const FString ExpectedPrefix = FString::Printf(TEXT("Quest.%s."), *FSimpleQuestEditorUtilities::SanitizeQuestlineTagSegment(HomeID));
        const FString CompiledTagStr = CompiledTag.GetTagName().ToString();
        if (!CompiledTagStr.StartsWith(ExpectedPrefix)) return Result;
        const FString RelativePath = CompiledTagStr.RightChop(ExpectedPrefix.Len());
        const FString SuffixToMatch = FString::Printf(TEXT(".%s"), *RelativePath);

        IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
        TArray<FAssetData> QuestlineAssets;
        AR.GetAssetsByClass(UQuestlineGraph::StaticClass()->GetClassPathName(), QuestlineAssets);

        for (const FAssetData& AssetData : QuestlineAssets)
        {
            if (AssetData.PackageName == HomePackageName) continue;

            const FString CompiledTagsJoined = AssetData.GetTagValueRef<FString>(TEXT("CompiledQuestTags"));
            if (CompiledTagsJoined.IsEmpty()) continue;

            const FString FriendlyStr = AssetData.GetTagValueRef<FString>(TEXT("FriendlyName"));
            const FText OuterDisplay = !FriendlyStr.IsEmpty() ? FText::FromString(FriendlyStr) : FText::FromName(AssetData.AssetName);

            TArray<FString> CompiledTagStrs;
            CompiledTagsJoined.ParseIntoArray(CompiledTagStrs, TEXT("|"));

            for (const FString& TagStr : CompiledTagStrs)
            {
                if (TagStr.Len() <= SuffixToMatch.Len()) continue;
                if (!TagStr.EndsWith(SuffixToMatch)) continue;

                const FGameplayTag ContextualTag = FGameplayTag::RequestGameplayTag(FName(*TagStr), false);
                if (!ContextualTag.IsValid()) continue;

                FQuestContextualTagMatch Match;
                Match.Tag = ContextualTag;
                Match.ParentAssetDisplayName = OuterDisplay;
                Result.Add(Match);
            }
        }
        return Result;
    }
}

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
	return FGameplayTag::RequestGameplayTag(FName(*TagName), false);
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

TArray<FSimpleQuestEditorUtilities::FQuestContextualActor> FSimpleQuestEditorUtilities::CollectContextualActorEntries(
	const UQuestlineNode_ContentBase* ContentNode,
	TFunctionRef<TArray<FString>(const FGameplayTag&)> TagToActorNames,
	const TCHAR* LogLabel)
{
	TArray<FQuestContextualActor> Result;
	const TArray<FQuestContextualTagMatch> Matches = CollectContextualTagMatchesForEditorNode(ContentNode);

	for (const FQuestContextualTagMatch& Match : Matches)
	{
		for (const FString& ActorName : TagToActorNames(Match.Tag))
		{
			FQuestContextualActor Entry;
			Entry.ActorName = ActorName;
			Entry.OuterAssetDisplayName = Match.ParentAssetDisplayName;
			Result.Add(Entry);
		}
	}

	// Sort + dedupe on (Outer, Actor).
	Result.Sort([](const FQuestContextualActor& A, const FQuestContextualActor& B)
	{
		const int32 Cmp = A.OuterAssetDisplayName.ToString().Compare(B.OuterAssetDisplayName.ToString());
		if (Cmp != 0) return Cmp < 0;
		return A.ActorName < B.ActorName;
	});
	for (int32 i = Result.Num() - 1; i > 0; --i)
	{
		if (Result[i].ActorName == Result[i - 1].ActorName
			&& Result[i].OuterAssetDisplayName.ToString() == Result[i - 1].OuterAssetDisplayName.ToString())
		{
			Result.RemoveAt(i, 1, EAllowShrinking::No);
		}
	}

	UE_LOG(LogSimpleQuest, Verbose, TEXT("%s: Node '%s' — %d contextual match(es) across OUTER assets"),
		LogLabel, *ContentNode->NodeLabel.ToString(), Result.Num());

	return Result;
}

TArray<FSimpleQuestEditorUtilities::FQuestContextualActor> FSimpleQuestEditorUtilities::FindContextualGiversForNode(const UQuestlineNode_ContentBase* ContentNode)
{
	return CollectContextualActorEntries(ContentNode, &FindActorNamesGivingTag, TEXT("FindContextualGiversForNode"));
}

TArray<FGameplayTag> FSimpleQuestEditorUtilities::CollectContextualNodeTagsForEditorNode(const UQuestlineNode_ContentBase* ContentNode)
{
	TArray<FGameplayTag> Result;
	for (const FQuestContextualTagMatch& Match : CollectContextualTagMatchesForEditorNode(ContentNode))
	{
		Result.AddUnique(Match.Tag);
	}
	return Result;
}

TArray<FSimpleQuestEditorUtilities::FQuestContextualActor> FSimpleQuestEditorUtilities::FindContextualWatchersForNode(const UQuestlineNode_ContentBase* ContentNode)
{
	return CollectContextualActorEntries(ContentNode, &FindActorNamesWatchingTag, TEXT("FindContextualWatchersForNode"));
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
	for (const auto& [TagName, NodeInstance] : CompiledNodes)
	{
		if (!NodeInstance)
		{
			continue;
		}
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

bool FSimpleQuestEditorUtilities::IsContentNodeTagCurrent(const UQuestlineNode_ContentBase* ContentNode)
{
	const FGameplayTag CompiledTag = FindCompiledTagForNode(ContentNode);
	if (!CompiledTag.IsValid())
	{
		UE_LOG(LogSimpleQuest, Verbose, TEXT("IsContentNodeTagCurrent: '%s' — FindCompiledTagForNode returned invalid (node likely not yet compiled)"),
			ContentNode ? *ContentNode->NodeLabel.ToString() : TEXT("(null)"));
		return false;
	}

	const FGameplayTag ReconstructedTag = ReconstructNodeTagInternal(ContentNode);
	if (!ReconstructedTag.IsValid())
	{
		UE_LOG(LogSimpleQuest, Verbose, TEXT("IsContentNodeTagCurrent: '%s' — Reconstructed tag invalid (label empty or Outer chain broken)"),
			*ContentNode->NodeLabel.ToString());
		return false;
	}

	const bool bMatch = (CompiledTag.GetTagName() == ReconstructedTag.GetTagName());
	return bMatch;
}

bool FSimpleQuestEditorUtilities::IsStepTagCurrent(const UQuestlineNode_Step* StepNode)
{
	return IsContentNodeTagCurrent(StepNode);
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

namespace
{
	/** Builds the project-wide compiled-tag universe via Asset Registry scan — no sync-load. */
	TSet<FName> BuildCompiledTagUniverse()
	{
		TSet<FName> Universe;

		IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		TArray<FAssetData> QuestlineAssets;
		AR.GetAssetsByClass(UQuestlineGraph::StaticClass()->GetClassPathName(), QuestlineAssets);

		for (const FAssetData& AssetData : QuestlineAssets)
		{
			const FString CompiledTagsJoined = AssetData.GetTagValueRef<FString>(TEXT("CompiledQuestTags"));
			if (CompiledTagsJoined.IsEmpty()) continue;

			TArray<FString> TagStrs;
			CompiledTagsJoined.ParseIntoArray(TagStrs, TEXT("|"));
			for (const FString& TagStr : TagStrs) Universe.Add(FName(*TagStr));
		}
		return Universe;
	}

	/** Creates a tokenized diagnostic with a clickable node-navigation action token appended. */
	TSharedRef<FTokenizedMessage> BuildValidationMessage(EMessageSeverity::Type Severity, const FText& LeadingText, const UEdGraphNode* TargetNode)
	{
		TSharedRef<FTokenizedMessage> Msg = FTokenizedMessage::Create(Severity);
		Msg->AddToken(FTextToken::Create(LeadingText));

		if (TargetNode)
		{
			TWeakObjectPtr<const UEdGraphNode> WeakNode = TargetNode;
			Msg->AddToken(FActionToken::Create(
				FText::FromString(TargetNode->GetNodeTitle(ENodeTitleType::ListView).ToString()),
				NSLOCTEXT("SimpleQuestEditor", "ValidateNavigateTooltip", "Navigate to this node in the graph editor"),
				FOnActionTokenExecuted::CreateLambda([WeakNode]()
				{
					if (const UEdGraphNode* Node = WeakNode.Get())
					{
						FSimpleQuestEditorUtilities::NavigateToEdGraphNode(Node);
					}
				})));
		}
		return Msg;
	}

	/** Recursively collects every UEdGraphNode reachable from this graph — through Quest inner graphs, etc. */
	void CollectAllNodesRecursive(const UEdGraph* Graph, TArray<UEdGraphNode*>& OutNodes)
	{
		if (!Graph) return;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node) continue;
			OutNodes.Add(Node);
			if (const UQuestlineNode_Quest* QuestNode = Cast<UQuestlineNode_Quest>(Node))
			{
				CollectAllNodesRecursive(QuestNode->GetInnerGraph(), OutNodes);
			}
		}
	}
}

FSimpleQuestEditorUtilities::FQuestTagValidationResult FSimpleQuestEditorUtilities::ValidateProjectPrereqTags()
{
	FQuestTagValidationResult Result;

	// Collected project-wide so we can cross-reference Rule Entries vs Rule Exits after the main walk.
	// Values: the emitting node, for FActionToken navigation on the unused-entry warning.
	TMap<FName, TWeakObjectPtr<const UEdGraphNode>> RuleEntryTags;
	TMap<FName, TArray<TWeakObjectPtr<const UEdGraphNode>>> RuleExitsByTag;

	IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	TArray<FAssetData> QuestlineAssets;
	AR.GetAssetsByClass(UQuestlineGraph::StaticClass()->GetClassPathName(), QuestlineAssets);

	for (const FAssetData& AssetData : QuestlineAssets)
	{
		UQuestlineGraph* Asset = Cast<UQuestlineGraph>(AssetData.GetAsset());
		if (!Asset || !Asset->QuestlineEdGraph) continue;

		TArray<UEdGraphNode*> AllNodes;
		CollectAllNodesRecursive(Asset->QuestlineEdGraph, AllNodes);

		for (UEdGraphNode* Node : AllNodes)
		{
			// Rule Entry: stash the GroupTag + weak node ref; cross-referenced post-scan.
			if (UQuestlineNode_PrerequisiteRuleEntry* RuleEntry = Cast<UQuestlineNode_PrerequisiteRuleEntry>(Node))
			{
				const FGameplayTag GroupTag = RuleEntry->GetGroupTag();
				if (GroupTag.IsValid())
				{
					RuleEntryTags.Add(GroupTag.GetTagName(), RuleEntry);
				}
				continue;
			}

			// Rule Exit: stash per-tag + unset-tag guard. Entry/Exit cross-reference happens post-walk so we catch stale-registered
			// tags too (the case where the Entry was deleted but its GroupTag lingers in the runtime tag manager).
			if (UQuestlineNode_PrerequisiteRuleExit* RuleExit = Cast<UQuestlineNode_PrerequisiteRuleExit>(Node))
			{
				const FGameplayTag GroupTag = RuleExit->GetGroupTag();
				if (GroupTag.IsValid())
				{
					RuleExitsByTag.FindOrAdd(GroupTag.GetTagName()).Add(RuleExit);
				}
				else
				{
					const FText Lead = FText::Format(
						NSLOCTEXT("SimpleQuestEditor", "ValidateRuleExitUnsetLead",
							"[{0}] Rule Exit has no GroupTag set:"),
						FText::FromString(Asset->GetName()));
					Result.Diagnostics.Emplace(BuildValidationMessage(EMessageSeverity::Error, Lead, RuleExit), EMessageSeverity::Error);
					++Result.ErrorCount;
				}
				continue;
			}

			// Content node: walk the prereq expression tree and check each leaf's fact tag against the runtime tag manager.
			UQuestlineNode_ContentBase* ContentNode = Cast<UQuestlineNode_ContentBase>(Node);
			if (!ContentNode) continue;

			const FPrereqExaminerTree Tree = CollectPrereqExpressionTopology(ContentNode);
			if (Tree.IsEmpty()) continue;

			for (const FPrereqExaminerNode& ExamNode : Tree.Nodes)
			{
				if (ExamNode.Type != EPrereqExaminerNodeType::Leaf) continue;
				if (FQuestStateTagUtils::IsTagRegisteredInRuntime(ExamNode.LeafTag)) continue;

				const UEdGraphNode* SourceForJump = ExamNode.SourceNode.IsValid() ? ExamNode.SourceNode.Get() : ContentNode;
				const FText Lead = FText::Format(
					NSLOCTEXT("SimpleQuestEditor", "ValidateLeafBrokenLead",
						"[{0}] Prereq leaf on '{1}' references missing fact '{2}':"),
					FText::FromString(Asset->GetName()),
					FText::FromString(ContentNode->GetNodeTitle(ENodeTitleType::ListView).ToString()),
					FText::FromString(ExamNode.LeafTag.IsValid() ? ExamNode.LeafTag.ToString() : TEXT("(unresolvable)")));

				Result.Diagnostics.Emplace(BuildValidationMessage(EMessageSeverity::Warning, Lead, SourceForJump), EMessageSeverity::Warning);
				++Result.WarningCount;
			}
		}
	}

	// Helper: resolve the owning UQuestlineGraph asset name from any emitting node, for the "[<Asset>]" prefix in leads.
	auto AssetNameForNode = [](const UEdGraphNode* Node) -> FString
	{
	    if (!Node || !Node->GetGraph()) return TEXT("<unknown asset>");
	    UObject* Outer = Node->GetGraph();
	    while (Outer && !Outer->IsA<UQuestlineGraph>()) Outer = Outer->GetOuter();
	    const UQuestlineGraph* Asset = Cast<UQuestlineGraph>(Outer);
	    return Asset ? Asset->GetName() : TEXT("<unknown asset>");
	};

	// Post-scan pass 1: Rule Exits whose GroupTag isn't produced by any Rule Entry in the project → Error per Exit.
	// This subsumes both "tag completely unregistered" and "tag still registered but Entry was deleted" cases.
	for (const auto& ExitPair : RuleExitsByTag)
	{
	    if (RuleEntryTags.Contains(ExitPair.Key)) continue;

	    for (const TWeakObjectPtr<const UEdGraphNode>& WeakExit : ExitPair.Value)
	    {
	        const UEdGraphNode* ExitNode = WeakExit.Get();
	        if (!ExitNode) continue;

	        const FText Lead = FText::Format(
	            NSLOCTEXT("SimpleQuestEditor", "ValidateRuleExitOrphanLead",
	                "[{0}] Rule Exit references rule '{1}' — no Rule Entry in the project provides this tag:"),
	            FText::FromString(AssetNameForNode(ExitNode)),
	            FText::FromName(ExitPair.Key));

	        Result.Diagnostics.Emplace(BuildValidationMessage(EMessageSeverity::Error, Lead, ExitNode), EMessageSeverity::Error);
	        ++Result.ErrorCount;
	    }
	}

	// Post-scan pass 2: Rule Entries that nobody references project-wide → Warning per Entry.
	for (const auto& EntryPair : RuleEntryTags)
	{
	    if (RuleExitsByTag.Contains(EntryPair.Key)) continue;

	    const UEdGraphNode* EntryNode = EntryPair.Value.Get();
	    if (!EntryNode) continue;

	    const FText Lead = FText::Format(
	        NSLOCTEXT("SimpleQuestEditor", "ValidateUnusedRuleEntryLead",
	            "[{0}] Rule Entry '{1}' is not referenced by any Rule Exit in the project. Remove it, or use Stale Quest Tags (Window → Developer Tools → Debug) to sweep unused rules alongside other stale references:"),
	        FText::FromString(AssetNameForNode(EntryNode)),
	        FText::FromName(EntryPair.Key));

	    Result.Diagnostics.Emplace(BuildValidationMessage(EMessageSeverity::Warning, Lead, EntryNode), EMessageSeverity::Warning);
	    ++Result.WarningCount;
	}

	UE_LOG(LogSimpleQuest, Log, TEXT("ValidateProjectPrereqTags: scanned %d asset(s), %d error(s), %d warning(s)"),
		QuestlineAssets.Num(), Result.ErrorCount, Result.WarningCount);

	return Result;
}

namespace
{
	/**
	 * Pure component-list stale-tag scan. Iterates Components, dispatches by type (Giver / Target / Watcher),
	 * emits one FStaleQuestTagEntry per stale tag found. Source / PackagePath / AssociatedActor are stamped on
	 * every entry; Component pointer is the specific component carrying the stale tag.
	 *
	 * Used by both the AActor-based scanner (Tier 1 / loaded-level path, where components come from
	 * Actor->GetComponents) AND the BP-CDO scanner (Tier 2, where components come from the CDO's native
	 * subobjects + the BP's SimpleConstructionScript template nodes + InheritableComponentHandler overrides).
	 */
	void ScanComponentsForStaleTags(
		const TArray<UActorComponent*>& Components,
		AActor* AssociatedActor,
		FSimpleQuestEditorUtilities::EStaleQuestTagSource Source,
		const FString& PackagePath,
		TArray<FSimpleQuestEditorUtilities::FStaleQuestTagEntry>& OutEntries)
	{
		using FStaleQuestTagEntry = FSimpleQuestEditorUtilities::FStaleQuestTagEntry;

		auto EmitIfStale = [&](UQuestComponentBase* Component, const FString& FieldLabel, const FGameplayTag& Tag)
		{
			if (FQuestStateTagUtils::IsTagRegisteredInRuntime(Tag)) return;
			FStaleQuestTagEntry Entry;
			Entry.Actor = AssociatedActor;
			Entry.Component = Component;
			Entry.FieldLabel = FieldLabel;
			Entry.StaleTag = Tag;
			Entry.Source = Source;
			Entry.PackagePath = PackagePath;
			OutEntries.Add(MoveTemp(Entry));
		};

		for (UActorComponent* Comp : Components)
		{
			if (!Comp) continue;

			if (UQuestGiverComponent* Giver = Cast<UQuestGiverComponent>(Comp))
			{
				for (const FGameplayTag& Tag : Giver->GetQuestTagsToGive())
					EmitIfStale(Giver, TEXT("QuestTagsToGive"), Tag);
			}
			else if (UQuestTargetComponent* Target = Cast<UQuestTargetComponent>(Comp))
			{
				for (const FGameplayTag& Tag : Target->GetStepTagsToWatch())
					EmitIfStale(Target, TEXT("StepTagsToWatch"), Tag);
			}
			else if (UQuestWatcherComponent* Watcher = Cast<UQuestWatcherComponent>(Comp))
			{
				for (const FGameplayTag& Tag : Watcher->GetWatchedStepTags())
					EmitIfStale(Watcher, TEXT("WatchedStepTags"), Tag);
				for (const auto& Pair : Watcher->GetWatchedTags())
					EmitIfStale(Watcher, TEXT("WatchedTags"), Pair.Key);
			}
		}
	}

	/**
	 * Actor-based convenience wrapper. Pulls every component off the actor via GetComponents and hands the list
	 * to ScanComponentsForStaleTags. Right behavior for Tier 1 (loaded-level instances where GetComponents
	 * returns the live component graph) — for BP CDOs the SCS / ICH paths give a more authoritative picture, see
	 * ScanActorBlueprintCDOs.
	 */
	void ScanActorForStaleTags(
		AActor* Actor,
		FSimpleQuestEditorUtilities::EStaleQuestTagSource Source,
		const FString& PackagePath,
		TArray<FSimpleQuestEditorUtilities::FStaleQuestTagEntry>& OutEntries)
	{
		if (!Actor) return;
		TArray<UActorComponent*> Components;
		Actor->GetComponents(Components);
		ScanComponentsForStaleTags(Components, Actor, Source, PackagePath, OutEntries);
	}

	/**
	 * Actor Blueprint CDO surface — Tier 2. For each actor-derived UBlueprint asset, gathers components from
	 * three sources and runs the shared ScanComponentsForStaleTags over the merged list:
	 *   1. Native components on the CDO (C++ CreateDefaultSubobject in the constructor chain).
	 *   2. SimpleConstructionScript nodes — components added via this BP's Components panel. These are template
	 *      instances whose property values represent what an actor instance gets at construction time.
	 *   3. InheritableComponentHandler records — overrides this BP applies to components inherited from a parent
	 *      BP. The override-template carries the authored property values used for instances of THIS BP.
	 *
	 * Walking all three is necessary because Actor->GetComponents on a CDO is unreliable for SCS / ICH-sourced
	 * components in the general case (depends on whether the BP has been compiled and the CDO recreated). The
	 * explicit walk catches all three of UQuestGiverComponent / UQuestTargetComponent / UQuestWatcherComponent
	 * regardless of how the designer added them.
	 *
	 * Pre-filters via AR-cached NativeParentClass tag so we don't sync-load non-actor BPs (UMG widget BPs, anim
	 * BPs, etc.). At typical project sizes the bounded sync-load cost is acceptable as a one-shot designer-driven
	 * action; Phase 4's Full Project Scan button wraps the whole pass in an FScopedSlowTask for the UI.
	 */
	void ScanActorBlueprintCDOs(TArray<FSimpleQuestEditorUtilities::FStaleQuestTagEntry>& OutEntries)
	{
		FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		IAssetRegistry& AR = ARM.Get();
		if (AR.IsLoadingAssets())
		{
			UE_LOG(LogSimpleQuest, Verbose, TEXT("ScanActorBlueprintCDOs: AssetRegistry still loading; waiting for completion before scan"));
			AR.WaitForCompletion();
		}

		FARFilter Filter;
		Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
		Filter.bRecursiveClasses = true;
		TArray<FAssetData> BlueprintAssets;
		AR.GetAssets(Filter, BlueprintAssets);

		// Pre-filter to actor-derived BPs via AR-cached NativeParentClass tag — avoids sync-loading UMG / anim /
		// widget blueprints. Tag value is in "Class'/Script/Engine.Actor'" wrapped form; ExportTextPathToObjectPath
		// strips the wrapper. FindObject<UClass> resolves against the in-memory class registry without loading.
		static const FName NativeParentClassTag(TEXT("NativeParentClass"));
		TArray<FAssetData> ActorBlueprints;
		ActorBlueprints.Reserve(BlueprintAssets.Num());
		for (const FAssetData& AssetData : BlueprintAssets)
		{
			FString ParentClassPath;
			if (!AssetData.GetTagValue(NativeParentClassTag, ParentClassPath)) continue;

			const FString ObjectPath = FPackageName::ExportTextPathToObjectPath(ParentClassPath);
			UClass* ParentClass = FindObject<UClass>(nullptr, *ObjectPath);
			if (!ParentClass) continue;
			if (!ParentClass->IsChildOf(AActor::StaticClass())) continue;

			ActorBlueprints.Add(AssetData);
		}

		for (const FAssetData& BPData : ActorBlueprints)
		{
			UBlueprint* BP = Cast<UBlueprint>(BPData.GetAsset());  // sync-load
			if (!BP) continue;

			UBlueprintGeneratedClass* BPGC = Cast<UBlueprintGeneratedClass>(BP->GeneratedClass);
			if (!BPGC) continue;

			AActor* CDOActor = Cast<AActor>(BPGC->GetDefaultObject());

			// Gather components from all three authoring surfaces, AddUnique-deduped (a single component reachable
			// via two paths only gets scanned once).
			TArray<UActorComponent*> Components;

			if (CDOActor)
			{
				CDOActor->GetComponents(Components);  // native + (potentially) SCS-resolved on the CDO
			}

			if (USimpleConstructionScript* SCS = BPGC->SimpleConstructionScript)
			{
				for (USCS_Node* Node : SCS->GetAllNodes())
				{
					if (Node && Node->ComponentTemplate)
					{
						Components.AddUnique(Node->ComponentTemplate);
					}
				}
			}

			if (UInheritableComponentHandler* ICH = BPGC->GetInheritableComponentHandler())
			{
				TArray<UActorComponent*> ICHTemplates;
				ICH->GetAllTemplates(ICHTemplates);
				for (UActorComponent* Template : ICHTemplates)
				{
					if (Template) Components.AddUnique(Template);
				}
			}

			if (Components.Num() == 0) continue;

			const FString PackagePath = BPData.PackageName.ToString();
			ScanComponentsForStaleTags(Components, CDOActor,
				FSimpleQuestEditorUtilities::EStaleQuestTagSource::ActorBlueprintCDO,
				PackagePath, OutEntries);
		}

		UE_LOG(LogSimpleQuest, Display,
			TEXT("ScanActorBlueprintCDOs: scanned %d actor-derived blueprint(s) of %d total blueprint(s) found in AR"),
			ActorBlueprints.Num(), BlueprintAssets.Num());
	}

	/**
	 * Predicate: does this BP's component tree (native CDO + SCS templates + ICH overrides) contain at least
	 * one UQuestComponentBase-derived component? Cheaper than a full scan because we bail on the first match.
	 */
	bool BlueprintHasAnyQuestComponent(UBlueprint* BP)
	{
		if (!BP) return false;
		UBlueprintGeneratedClass* BPGC = Cast<UBlueprintGeneratedClass>(BP->GeneratedClass);
		if (!BPGC) return false;

		auto AnyQuestComponent = [](const TArray<UActorComponent*>& Components) -> bool
		{
			for (UActorComponent* Comp : Components)
			{
				if (Comp && Comp->IsA<UQuestComponentBase>()) return true;
			}
			return false;
		};

		if (AActor* CDOActor = Cast<AActor>(BPGC->GetDefaultObject(false)))
		{
			TArray<UActorComponent*> Components;
			CDOActor->GetComponents(Components);
			if (AnyQuestComponent(Components)) return true;
		}

		if (USimpleConstructionScript* SCS = BPGC->SimpleConstructionScript)
		{
			TArray<UActorComponent*> SCSTemplates;
			for (USCS_Node* Node : SCS->GetAllNodes())
			{
				if (Node && Node->ComponentTemplate) SCSTemplates.Add(Node->ComponentTemplate);
			}
			if (AnyQuestComponent(SCSTemplates)) return true;
		}

		if (UInheritableComponentHandler* ICH = BPGC->GetInheritableComponentHandler())
		{
			TArray<UActorComponent*> ICHTemplates;
			ICH->GetAllTemplates(ICHTemplates);
			if (AnyQuestComponent(ICHTemplates)) return true;
		}

		return false;
	}

	/**
	 * Builds the set of UClass* values representing "actor classes known to author a UQuestComponentBase
	 * descendant in their default component tree." Used by ScanWorldPartitionActors's class-filter opt-in
	 * (FStaleTagScanScope::bComprehensiveWPScan = false) to skip sync-loading WP actors that can't possibly
	 * carry a quest tag.
	 *
	 * Two-pass build:
	 *   1. Native UClass walk via TObjectIterator — catches AActor descendants with C++-added quest
	 *      components (no BP wrapper required). Cheap; no asset loads.
	 *   2. Actor-derived BP walk via the AR — catches BP-authored quest components. Sync-loads each
	 *      actor BP; same cost model as ScanActorBlueprintCDOs's BP walk.
	 *
	 * Built only when the caller opts into the filter (comprehensive mode skips the build entirely).
	 */
	TSet<UClass*> BuildQuestComponentClassSet()
	{
		TSet<UClass*> Result;

		// Pass 1: native classes
		for (TObjectIterator<UClass> It; It; ++It)
		{
			UClass* Cls = *It;
			if (!Cls || Cls->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated)) continue;
			if (!Cls->IsChildOf(AActor::StaticClass())) continue;

			// Skip BP-generated classes — Pass 2 handles those via the BP iteration which gets the full
			// component tree (SCS + ICH). The CDO-only walk here would miss SCS/ICH-added components.
			if (Cls->ClassGeneratedBy != nullptr) continue;

			AActor* CDO = Cast<AActor>(Cls->GetDefaultObject(false));
			if (!CDO) continue;

			TArray<UActorComponent*> Components;
			CDO->GetComponents(Components);
			for (UActorComponent* Comp : Components)
			{
				if (Comp && Comp->IsA<UQuestComponentBase>())
				{
					Result.Add(Cls);
					break;
				}
			}
		}

		// Pass 2: BPs
		FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		IAssetRegistry& AR = ARM.Get();
		if (AR.IsLoadingAssets()) AR.WaitForCompletion();

		FARFilter Filter;
		Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
		Filter.bRecursiveClasses = true;
		TArray<FAssetData> BlueprintAssets;
		AR.GetAssets(Filter, BlueprintAssets);

		static const FName NativeParentClassTag(TEXT("NativeParentClass"));
		for (const FAssetData& AssetData : BlueprintAssets)
		{
			FString ParentClassPath;
			if (!AssetData.GetTagValue(NativeParentClassTag, ParentClassPath)) continue;
			const FString ObjectPath = FPackageName::ExportTextPathToObjectPath(ParentClassPath);
			UClass* ParentClass = FindObject<UClass>(nullptr, *ObjectPath);
			if (!ParentClass || !ParentClass->IsChildOf(AActor::StaticClass())) continue;

			UBlueprint* BP = Cast<UBlueprint>(AssetData.GetAsset());  // sync-load
			if (!BP) continue;

			if (BlueprintHasAnyQuestComponent(BP))
			{
				if (UClass* GenClass = BP->GeneratedClass)
				{
					Result.Add(GenClass);
				}
			}
		}

		UE_LOG(LogSimpleQuest, Verbose,
			TEXT("BuildQuestComponentClassSet: %d quest-component-bearing classes (native + BP combined)"),
			Result.Num());
		return Result;
	}

	/**
	 * WP-level actor scanner. Iterates every WP actor descriptor via FWorldPartitionHelpers::ForEachActorWithLoading
	 * (load → callback → unload per actor, the same iteration the cooker uses). Optional class filter skips
	 * descriptors whose actor class isn't in the quest-component class set — set to nullptr for comprehensive mode.
	 *
	 * The comprehensive path sync-loads every WP actor; on a 10k-actor WP level this is slow but thorough. The
	 * filtered path resolves the class from the descriptor before loading the actor, skipping most of the cost.
	 */
	void ScanWorldPartitionActors(UWorld* World, const FString& PackagePath,
		const TSet<UClass*>* ClassFilter,
		TArray<FSimpleQuestEditorUtilities::FStaleQuestTagEntry>& OutEntries)
	{
		UWorldPartition* WP = World ? World->GetWorldPartition() : nullptr;
		if (!WP) return;

		int32 NumDescriptors = 0;
		int32 NumScanned = 0;
		int32 NumFilteredByClass = 0;

		FWorldPartitionHelpers::ForEachActorWithLoading(WP,
			[&](const FWorldPartitionActorDescInstance* DescInstance) -> bool
			{
				++NumDescriptors;
				if (!DescInstance) return true;

				if (ClassFilter)
				{
					// Resolve actor class from descriptor without forcing actor load. GetActorNativeClass returns
					// the native ancestor; we want the actual actor class (BP-generated or native). Descriptor
					// stores the class path; LoadClass loads just the UClass metadata, not the actor instance.
					UClass* ActorClass = nullptr;
					if (UClass* NativeClass = DescInstance->GetActorNativeClass())
					{
						ActorClass = NativeClass;
					}
					// Some UE 5.6 descriptor variants expose GetBaseClass / GetActorClass / GetActorClassPath —
					// if NativeClass alone misses BP-generated subclasses, the LoadClass fallback below catches
					// them. (Bridge code; remove the LoadClass branch if NativeClass is already the BP class.)

					bool bMatches = false;
					if (ActorClass)
					{
						for (UClass* AllowedClass : *ClassFilter)
						{
							if (ActorClass->IsChildOf(AllowedClass)) { bMatches = true; break; }
						}
					}
					if (!bMatches)
					{
						++NumFilteredByClass;
						return true;
					}
				}

				if (AActor* Actor = DescInstance->GetActor())
				{
					ScanActorForStaleTags(Actor,
						FSimpleQuestEditorUtilities::EStaleQuestTagSource::UnloadedLevelInstance,
						PackagePath, OutEntries);
					++NumScanned;
				}
				return true;
			});

		UE_LOG(LogSimpleQuest, Display,
			TEXT("ScanWorldPartitionActors: %s — %d descriptors, %d scanned, %d filtered by class (filter=%s)"),
			*PackagePath, NumDescriptors, NumScanned, NumFilteredByClass,
			ClassFilter ? TEXT("on") : TEXT("off"));
	}

	/**
	 * Unloaded-level surface — Tier 2. Iterates every UWorld asset in the project via the Asset Registry,
	 * skips any that are currently loaded (those are covered by ScanLoadedLevels — avoid double-counting),
	 * sync-loads each remaining umap, and dispatches by world type:
	 *   - Non-WP world: walks PersistentLevel->Actors and runs each through ScanActorForStaleTags.
	 *   - WP-enabled world: hands off to ScanWorldPartitionActors, which uses
	 *     FWorldPartitionHelpers::ForEachActorWithLoading to iterate the actor descriptor database with
	 *     per-actor load/unload (the cooker's iteration pattern). Optional class-filter set, built lazily
	 *     from BuildQuestComponentClassSet, skips descriptors whose class can't carry a quest component;
	 *     comprehensive mode (Scope.bComprehensiveWPScan = true, default) skips the filter and loads every
	 *     actor.
	 *
	 * Loading semantics: FAssetData::GetAsset sync-loads the umap as an asset (NOT as the editor's current
	 * world — no PIE init, no BeginPlay, no sublevel streaming). For non-WP worlds, persistent-level actor
	 * iteration gives us the design-time actor set authored into the umap. For WP worlds, the descriptor
	 * walk gives us the full external-actor set without needing to load actors that won't be scanned.
	 *
	 * Cleanup: sync-loaded non-WP worlds stay resident until the next GC. Acceptable for a one-shot
	 * designer-driven scan; aggressive unload after the scan would risk corrupting the editor if any
	 * sublevel reference is already held by the editor's main world. WP descriptors handle their own
	 * per-actor unload via FWorldPartitionHelpers, so memory pressure scales with class-filter strictness
	 * rather than total actor count.
	 *
	 * Class-filter gap (only when bComprehensiveWPScan = false): per-instance component additions —
	 * dropping a UQuestComponentBase onto a single placed actor instance without modifying its BP — won't
	 * pass the filter because the instance's class isn't in the quest-component class set. Comprehensive
	 * mode catches it. Documented as a deliberate trade in FStaleTagScanScope::bComprehensiveWPScan.
	 */
	void ScanUnloadedLevels(const FSimpleQuestEditorUtilities::FStaleTagScanScope& Scope, TArray<FSimpleQuestEditorUtilities::FStaleQuestTagEntry>& OutEntries)
	{
		FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		IAssetRegistry& AR = ARM.Get();
		if (AR.IsLoadingAssets())
		{
			UE_LOG(LogSimpleQuest, Verbose, TEXT("ScanUnloadedLevels: AssetRegistry still loading; waiting for completion before scan"));
			AR.WaitForCompletion();
		}

		// Build class filter set lazily — only when the user opted out of comprehensive WP mode AND we'll
		// actually encounter a WP level. Cheaper to defer than to build unconditionally.
		TOptional<TSet<UClass*>> ClassFilter;
		auto EnsureClassFilter = [&]() -> const TSet<UClass*>*
		{
			if (Scope.bComprehensiveWPScan) return nullptr;
			if (!ClassFilter.IsSet()) ClassFilter = BuildQuestComponentClassSet();
			return &ClassFilter.GetValue();
		};

		// Build a snapshot of editor-active world packages — these are Tier 1's territory and should not be
		// re-scanned here. Note we deliberately do NOT use FindPackage as the skip predicate: a world that
		// was sync-loaded by a previous ScanUnloadedLevels call stays resident in memory (per the documented
		// "stays resident until GC" behavior) and FindPackage would return non-null for it on subsequent
		// scans — but that world is NOT an active editor world, so Tier 1 wouldn't pick it up either. Using
		// GetWorldContexts ensures we only skip worlds Tier 1 actually covers; everything else gets scanned,
		// regardless of whether it happens to already be in memory.
		TSet<FName> EditorWorldPackages;
		if (GEditor)
		{
			for (const FWorldContext& Context : GEditor->GetWorldContexts())
			{
				if (Context.WorldType != EWorldType::Editor) continue;
				UWorld* ActiveWorld = Context.World();
				if (!ActiveWorld) continue;
				if (UPackage* Pkg = ActiveWorld->GetOutermost())
				{
					EditorWorldPackages.Add(Pkg->GetFName());
				}
			}
		}

		FARFilter Filter;
		Filter.ClassPaths.Add(UWorld::StaticClass()->GetClassPathName());
		Filter.bRecursiveClasses = false;
		TArray<FAssetData> WorldAssets;
		AR.GetAssets(Filter, WorldAssets);

		int32 NumAlreadyLoaded = 0;
		int32 NumNonWPScanned = 0;
		int32 NumWPScanned = 0;
		int32 NumLoadFailed = 0;

		for (const FAssetData& WorldData : WorldAssets)
		{
			const FString PackagePath = WorldData.PackageName.ToString();

			if (EditorWorldPackages.Contains(WorldData.PackageName))
			{
				++NumAlreadyLoaded;
				continue;
			}

			// Either truly unloaded, OR in memory from a prior ScanUnloadedLevels run. Either way we want to
			// scan it now — reuse the already-resident world if possible to skip a redundant sync-load.
			UWorld* World = nullptr;
			if (UPackage* ExistingPkg = FindPackage(nullptr, *PackagePath))
			{
				World = UWorld::FindWorldInPackage(ExistingPkg);
			}
			if (!World)
			{
				World = Cast<UWorld>(WorldData.GetAsset());  // sync-load
			}
			if (!World)
			{
				++NumLoadFailed;
				continue;
			}

			if (World->IsPartitionedWorld() && World->GetWorldPartition())
			{
				// Sync-loading a UWorld asset via FAssetData::GetAsset doesn't initialize the world or populate
				// its WP actor descriptor container — those happen during the editor's Map_Load pipeline, which
				// we're not on. Without init, FWorldPartitionHelpers::ForEachActorWithLoading walks an empty
				// container and finds nothing.
				//
				// Lifecycle is messier than just InitWorld: in commandlet mode the helper's per-batch and
				// end-of-iteration GC passes use RF_NoFlags as KeepFlags (WorldPartitionHelpers.cpp:268) — only
				// root-set objects survive. Our sync-loaded World has no rooted referencer, so it goes
				// unreachable mid-scan; UWorldPartition::BeginDestroy / UTickableWorldSubsystem::BeginDestroy
				// then assert because the WP and its tickable subsystems are still initialized.
				//
				// FScopedEditorWorld is the engine's blessed RAII helper for this exact case (used by the WP
				// convert commandlet, etc.). It handles: AddToRoot, GWorld + EditorWorldContext swap, InitWorld,
				// UpdateModelComponents, UpdateWorldComponents, UpdateLevelStreaming on construction; and
				// DestroyWorld (which routes through CleanupWorld + subsystem deinit + WP Uninitialize) +
				// RemoveFromRoot + GWorld restore on destruction. We init as Editor type and disable all
				// play-mode subsystems — we just want WP iteration, no physics / nav / AI / audio overhead.
				if (!World->bIsWorldInitialized)
				{
					FScopedEditorWorld ScopedWorld(World, UWorld::InitializationValues()
						.ShouldSimulatePhysics(false)
						.EnableTraceCollision(false)
						.CreateNavigation(false)
						.CreateAISystem(false)
						.AllowAudioPlayback(false)
						.CreatePhysicsScene(false), EWorldType::Editor);

					ScanWorldPartitionActors(World, PackagePath, EnsureClassFilter(), OutEntries);
				}
				else
				{
					// Already-initialized (resident from a prior run still pending GC, or some external owner
					// holds it). Scan directly without taking ownership of the lifecycle — FScopedEditorWorld
					// asserts on already-initialized worlds and would interfere with the real owner anyway.
					UE_LOG(LogSimpleQuest, Verbose,
						TEXT("ScanUnloadedLevels: '%s' already initialized; scanning without lifecycle wrapper"),
						*PackagePath);
					ScanWorldPartitionActors(World, PackagePath, EnsureClassFilter(), OutEntries);
				}
				++NumWPScanned;
			}
			else if (ULevel* PersistentLevel = World->PersistentLevel)
			{
				int32 NumActorsInLevel = 0;
				for (AActor* Actor : PersistentLevel->Actors)
				{
					if (!Actor) continue;
					ScanActorForStaleTags(Actor,
						FSimpleQuestEditorUtilities::EStaleQuestTagSource::UnloadedLevelInstance,
						PackagePath, OutEntries);
					++NumActorsInLevel;
				}
				UE_LOG(LogSimpleQuest, Display,
					TEXT("ScanUnloadedLevels: non-WP world '%s' — %d actors walked"),
					*PackagePath, NumActorsInLevel);
				++NumNonWPScanned;
			}
		}

		UE_LOG(LogSimpleQuest, Display,
			TEXT("ScanUnloadedLevels: %d total worlds in AR; %d already loaded (Tier 1), %d non-WP scanned, %d WP scanned (mode=%s), %d load failures"),
			WorldAssets.Num(), NumAlreadyLoaded, NumNonWPScanned, NumWPScanned,
			Scope.bComprehensiveWPScan ? TEXT("comprehensive") : TEXT("class-filtered"),
			NumLoadFailed);
	}

	/**
	 * Loaded-level surface — Tier 1 baseline. Walks GEditor->GetWorldContexts and scans every actor in every
	 * editor-type world. Carries the world's package path on each entry for consistency with the Tier 2 surfaces
	 * (the panel doesn't currently use it for loaded entries since the actor pointer is enough, but the field
	 * is populated for the commandlet's structured output).
	 */
	void ScanLoadedLevels(TArray<FSimpleQuestEditorUtilities::FStaleQuestTagEntry>& OutEntries)
	{
		if (!GEditor) return;

		for (const FWorldContext& Context : GEditor->GetWorldContexts())
		{
			UWorld* World = Context.World();
			if (!World || Context.WorldType != EWorldType::Editor) continue;

			const FString WorldPackagePath = World->GetOutermost() ? World->GetOutermost()->GetName() : FString();

			for (TActorIterator<AActor> It(World); It; ++It)
			{
				ScanActorForStaleTags(*It, FSimpleQuestEditorUtilities::EStaleQuestTagSource::LoadedLevelInstance,
					WorldPackagePath, OutEntries);
			}
		}
	}
}

TArray<FSimpleQuestEditorUtilities::FStaleQuestTagEntry> FSimpleQuestEditorUtilities::CollectStaleQuestTagEntries(FStaleTagScanScope Scope)
{
	TArray<FStaleQuestTagEntry> Result;

	if (Scope.bLoadedLevels)
	{
		ScanLoadedLevels(Result);
	}
	if (Scope.bActorBlueprintCDOs)
	{
		ScanActorBlueprintCDOs(Result);
	}
	if (Scope.bUnloadedLevels)
	{
		ScanUnloadedLevels(Scope, Result);
	}
	
	UE_LOG(LogSimpleQuest, Display,
		TEXT("CollectStaleQuestTagEntries: %d stale reference(s) found (scope flags: loaded=%s, bpCDOs=%s, unloaded=%s)"),
		Result.Num(),
		Scope.bLoadedLevels ? TEXT("on") : TEXT("off"),
		Scope.bActorBlueprintCDOs ? TEXT("on") : TEXT("off"),
		Scope.bUnloadedLevels ? TEXT("on") : TEXT("off"));

	return Result;
}

// =============================================================================
// TEMPORARY VERIFICATION COMMAND — Phase 2 of Stale Quest Tags Tier 2
// Remove this entire block once Phase 4's panel button is in place.
// =============================================================================
//
// Console command: SimpleQuest.Debug.ScanBlueprintCDOs
// Runs CollectStaleQuestTagEntries with ONLY the actor-BP-CDO scope bit set,
// prints one Display line per entry to LogSimpleQuest. Useful for verifying
// Phase 2's ScanActorBlueprintCDOs helper without needing the panel UI.
//
// Usage:
//   1. Build editor.
//   2. Open the project.
//   3. In the Output Log, type:  SimpleQuest.Debug.ScanBlueprintCDOs
//   4. Look at LogSimpleQuest at Display+ verbosity. Expect a header line
//      with the entry count, then one line per stale tag found.
//
// Set up a known-stale BP to test against (per Phase 2 verification step 1-5)
// and confirm the entry shows up here. Then delete the test BP.

#include "HAL/IConsoleManager.h"

static FAutoConsoleCommand GSimpleQuestDebugScanBlueprintCDOsCmd(
	TEXT("SimpleQuest.Debug.ScanBlueprintCDOs"),
	TEXT("Run the Tier 2 actor-Blueprint-CDO stale-tag scan and dump results to LogSimpleQuest. Diagnostic only."),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		FSimpleQuestEditorUtilities::FStaleTagScanScope Scope;
		Scope.bLoadedLevels       = false;
		Scope.bActorBlueprintCDOs = true;
		Scope.bUnloadedLevels     = false;

		const TArray<FSimpleQuestEditorUtilities::FStaleQuestTagEntry> Entries =
			FSimpleQuestEditorUtilities::CollectStaleQuestTagEntries(Scope);

		UE_LOG(LogSimpleQuest, Display, TEXT("---- BP CDO stale-tag scan: %d entries ----"), Entries.Num());
		for (const FSimpleQuestEditorUtilities::FStaleQuestTagEntry& E : Entries)
		{
			const FString CDOClassName = E.Actor.IsValid() ? E.Actor->GetClass()->GetName() : TEXT("(null)");
			UE_LOG(LogSimpleQuest, Display, TEXT("  [BP CDO] class=%s field=%s tag=%s package=%s"),
				*CDOClassName,
				*E.FieldLabel,
				*E.StaleTag.ToString(),
				*E.PackagePath);
		}
		UE_LOG(LogSimpleQuest, Display, TEXT("---- end of BP CDO scan ----"));
	})
);

static FAutoConsoleCommand GSimpleQuestDebugScanUnloadedLevelsCmd(
	TEXT("SimpleQuest.Debug.ScanUnloadedLevels"),
	TEXT("Run the Tier 2 unloaded-level stale-tag scan and dump results to LogSimpleQuest. Diagnostic only."),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		FSimpleQuestEditorUtilities::FStaleTagScanScope Scope;
		Scope.bLoadedLevels = false;
		Scope.bActorBlueprintCDOs = false;
		Scope.bUnloadedLevels = true;

		const TArray<FSimpleQuestEditorUtilities::FStaleQuestTagEntry> Entries =
			FSimpleQuestEditorUtilities::CollectStaleQuestTagEntries(Scope);

		UE_LOG(LogSimpleQuest, Display, TEXT("---- Unloaded-level stale-tag scan: %d entries ----"), Entries.Num());
		for (const FSimpleQuestEditorUtilities::FStaleQuestTagEntry& E : Entries)
		{
			const FString ActorName = E.Actor.IsValid() ? E.Actor->GetActorNameOrLabel() : TEXT("(stale weak ptr)");
			UE_LOG(LogSimpleQuest, Display, TEXT("  [Unloaded] actor=%s field=%s tag=%s package=%s"),
				*ActorName,
				*E.FieldLabel,
				*E.StaleTag.ToString(),
				*E.PackagePath);
		}
		UE_LOG(LogSimpleQuest, Display, TEXT("---- end of unloaded-level scan ----"));
	})
);

