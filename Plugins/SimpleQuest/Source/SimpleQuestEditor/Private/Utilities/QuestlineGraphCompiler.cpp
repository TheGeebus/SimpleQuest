// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Utilities/QuestlineGraphCompiler.h"

#include "GameplayTagsManager.h"
#include "ISimpleQuestEditorModule.h"
#include "SimpleQuestLog.h"
#include "Quests/QuestlineGraph.h"
#include "Quests/QuestNodeBase.h"
#include "Quests/QuestStep.h"
#include "Quests/Quest.h"
#include "Quests/PrerequisiteExpression.h"
#include "Quests/QuestPrereqGroupNode.h"
#include "Quests/SetBlockedNode.h"
#include "Quests/ClearBlockedNode.h"
#include "Quests/ActivationGroupSetterNode.h"
#include "Quests/ActivationGroupGetterNode.h"
#include "Nodes/QuestlineNode_ContentBase.h"
#include "Nodes/QuestlineNode_Quest.h"
#include "Nodes/QuestlineNode_Step.h"
#include "Nodes/QuestlineNode_LinkedQuestline.h"
#include "Nodes/QuestlineNode_Knot.h"
#include "Nodes/QuestlineNode_Entry.h"
#include "Nodes/QuestlineNode_Exit.h"
#include "Nodes/Prerequisites/QuestlineNode_PrerequisiteAnd.h"
#include "Nodes/Prerequisites/QuestlineNode_PrerequisiteOr.h"
#include "Nodes/Prerequisites/QuestlineNode_PrerequisiteNot.h"
#include "Nodes/Groups/QuestlineNode_PrerequisiteGroupSetter.h"
#include "Nodes/Groups/QuestlineNode_PrerequisiteGroupGetter.h"
#include "Nodes/Utility/QuestlineNode_SetBlocked.h"
#include "Nodes/Utility/QuestlineNode_ClearBlocked.h"
#include "Nodes/Groups/QuestlineNode_ActivationGroupSetter.h"
#include "Nodes/Groups/QuestlineNode_ActivationGroupGetter.h"
#include "Utilities/QuestStateTagUtils.h"
#include "Utilities/QuestlineGraphTraversalPolicy.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Objectives/QuestObjective.h"
#include "Rewards/QuestReward.h"
#include "Toolkit/QuestlineGraphEditor.h"
#include "Utilities/SimpleQuestEditorUtils.h"



FQuestlineGraphCompiler::FQuestlineGraphCompiler()
    : TraversalPolicy(MakeUnique<FQuestlineGraphTraversalPolicy>())
{
}

FQuestlineGraphCompiler::~FQuestlineGraphCompiler() = default;


// -------------------------------------------------------------------------------------------------
// Entry point
// -------------------------------------------------------------------------------------------------

bool FQuestlineGraphCompiler::Compile(UQuestlineGraph* InGraph)
{
    if (!InGraph || !InGraph->QuestlineEdGraph)
    {
        AddError(TEXT("Invalid graph asset. QuestlineEdGraph is null."));
        return false;
    }

    bHasErrors = false;
    Messages.Empty();
    NumErrors = 0;
    NumWarnings = 0;
    RootGraph = InGraph;

    // Derive the effective questline ID; designer override takes priority, asset name is the fallback
    const FString TagPrefix = SanitizeTagSegment(InGraph->QuestlineID.IsEmpty() ? InGraph->GetName() : InGraph->QuestlineID);

    // Validate that no other questline asset shares this effective ID
    IAssetRegistry& AssetRegistry = FAssetRegistryModule::GetRegistry();
    TArray<FAssetData> AllQuestlineGraphs;
    FARFilter Filter;
    Filter.ClassPaths.Add(UQuestlineGraph::StaticClass()->GetClassPathName());
    Filter.bRecursiveClasses = true;
    AssetRegistry.GetAssets(Filter, AllQuestlineGraphs);

    for (const FAssetData& Asset : AllQuestlineGraphs)
    {
        if (Asset.GetObjectPathString() == InGraph->GetPathName()) continue;
        FAssetTagValueRef TagValue = Asset.TagsAndValues.FindTag(TEXT("QuestlineEffectiveID"));
        if (TagValue.IsSet() && SanitizeTagSegment(TagValue.GetValue()) == TagPrefix)
        {
            AddError(FString::Printf(
                TEXT("QuestlineID '%s' is already used by '%s'. Set a unique QuestlineID on one of these assets to resolve the conflict."),
                *TagPrefix,
                *Asset.GetObjectPathString()));
            return false;
        }
    }
    
    UE_LOG(LogSimpleQuest, Log, TEXT("Compile: starting '%s' (prefix='%s')"),
        *InGraph->GetName(),
        *TagPrefix);

    // ── Snapshot old GUID→Tag mapping for rename detection ────────
    TMap<FGuid, FName> OldTagsByGuid;
    for (const auto& [TagName, NodeInstance] : InGraph->CompiledNodes)
    {
        if (NodeInstance && NodeInstance->GetQuestGuid().IsValid())
        {
            OldTagsByGuid.Add(NodeInstance->GetQuestGuid(), TagName);
        }
    }
	CurrentOuterGuidChain = FGuid();
    DetectedTagRenames.Empty();

    InGraph->Modify();
    InGraph->CompiledNodes.Empty(); 
    InGraph->EntryNodeTags.Empty();
    InGraph->CompiledQuestTags.Empty();
    AllCompiledNodes.Empty();
    UtilityNodeKeyMap.Empty();
    RootGraph = InGraph;

    // Refresh outcome pins on all step nodes so that changes to outcomes on an objective class are reflected without
    // the designer having to touch ObjectiveClass again.
    for (UEdGraphNode* Node : InGraph->QuestlineEdGraph->Nodes)
    {
        if (UQuestlineNode_Step* StepNode = Cast<UQuestlineNode_Step>(Node)) StepNode->RefreshOutcomePins();
    }

    // The graphs that have already been compiled. Provided to CompileGraph, which forwards it to all recursive calls.
    TArray<FString> VisitedAssetPaths;
    VisitedAssetPaths.Add(InGraph->GetPathName());

    // Start recursive compilation, working forward from the Start node.
    TArray<FName> EntryTags = CompileGraph(InGraph->QuestlineEdGraph, TagPrefix, {}, VisitedAssetPaths);
    InGraph->EntryNodeTags = EntryTags;
    InGraph->CompiledNodes = MoveTemp(AllCompiledNodes);
    InGraph->CompiledEditorNodes = MoveTemp(AllCompiledEditorNodes);
    InGraph->CompiledQuestTags = MoveTemp(AllCompiledQuestTags);

    // Detect renames via GUID bridge
    DetectAndRecordTagRenames(InGraph, OldTagsByGuid);

    RegisterCompiledTags(InGraph);

    UE_LOG(LogSimpleQuest, Log, TEXT("Compile: '%s' finished — %d node(s), %d tag(s), %d error(s), %d warning(s)"),
        *InGraph->GetName(),
        InGraph->CompiledNodes.Num(),
        InGraph->CompiledQuestTags.Num(),
        NumErrors,
        NumWarnings);
    
    return !bHasErrors;
}


// -------------------------------------------------------------------------------------------------
// CompileGraph — recursive
// -------------------------------------------------------------------------------------------------

TArray<FName> FQuestlineGraphCompiler::CompileGraph(UEdGraph* Graph, const FString& TagPrefix, const TMap<FGameplayTag, TArray<FName>>& BoundaryTagsByOutcome, TArray<FString>& VisitedAssetPaths, TMap<FGameplayTag, FQuestEntryRouteList>* OutEntryTagsByOutcome)	
{
    if (!Graph) return {};

    // ---- Pass 1: label uniqueness, GUID write, tag assignment ----
    TArray<UQuestlineNode_ContentBase*> ContentNodes;
    TMap<UQuestlineNode_ContentBase*, UQuestNodeBase*> NodeInstanceMap;
    CompileNodeRegistration(Graph, TagPrefix, BoundaryTagsByOutcome, VisitedAssetPaths, ContentNodes, NodeInstanceMap);

    // ---- Pass 1b: setter nodes — create UQuestPrereqGroupNode monitors ----
    TArray<FName> MonitorTags;
    TArray<FName> GetterEntryTags;
    CompileGroupSetters(Graph, TagPrefix, MonitorTags, GetterEntryTags);

    // ---- Pass 1c: utility nodes ----
    TArray<UQuestlineNode_UtilityBase*> UtilityEdNodes;
    CompileUtilityNodes(Graph, UtilityEdNodes);

    UE_LOG(LogSimpleQuest, Verbose, TEXT("CompileGraph: [%s] %d content, %d group setter(s), %d utility node(s)"),
        *TagPrefix,
        ContentNodes.Num(),
        MonitorTags.Num(),
        UtilityEdNodes.Num());
    
    if (bHasErrors) return {};

    // ---- Stale pin diagnostic ----
    for (UQuestlineNode_ContentBase* ContentNode : ContentNodes)
    {
        for (UEdGraphPin* Pin : ContentNode->Pins)
        {
            if (Pin->bOrphanedPin && Pin->LinkedTo.Num() > 0)
            {
                const FString Label = ContentNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
                AddWarning(FString::Printf(
                    TEXT("[%s] Node '%s' has a stale pin '%s' with %d active connection(s). These wires will be ignored at runtime. Right-click the node to remove stale pins."),
                    *TagPrefix, *Label, *Pin->PinName.ToString(), Pin->LinkedTo.Num()), ContentNode);
            }
        }
    }

    // ---- Pass 2: output pin wiring ----
    CompileOutputWiring(ContentNodes, NodeInstanceMap, TagPrefix, BoundaryTagsByOutcome, VisitedAssetPaths);

    // ---- Pass 2b: Forward output wiring for all utility-keyed nodes ----
    for (auto& [EdNode, UtilKey] : UtilityNodeKeyMap)
    {
        UQuestNodeBase* Inst = AllCompiledNodes.FindRef(UtilKey);
        if (!Inst) continue;

        Inst->NextNodesOnForward.Empty();

        if (UEdGraphPin* ForwardPin = EdNode->FindPin(TEXT("Forward"), EGPD_Output))
        {
            TArray<FName> ForwardTags;
            ResolvePinToTags(ForwardPin, TagPrefix, BoundaryTagsByOutcome, VisitedAssetPaths, ForwardTags);
            for (const FName& Tag : ForwardTags) Inst->NextNodesOnForward.Add(Tag);
        }
    }

    // ---- Resolve entry tags from the graph's Entry node ----
    TArray<FName> EntryTags = ResolveEntryTags(Graph, TagPrefix, BoundaryTagsByOutcome, VisitedAssetPaths, OutEntryTagsByOutcome);
    EntryTags.Append(MonitorTags);
    EntryTags.Append(GetterEntryTags);
    return EntryTags;
}

void FQuestlineGraphCompiler::CompileNodeRegistration(UEdGraph* Graph, const FString& TagPrefix, const TMap<FGameplayTag, TArray<FName>>& BoundaryTagsByOutcome, TArray<FString>& VisitedAssetPaths, TArray<UQuestlineNode_ContentBase*>& OutContentNodes, TMap<UQuestlineNode_ContentBase*, UQuestNodeBase*>& OutNodeInstanceMap)
{
    TMap<FString, UQuestlineNode_ContentBase*> LabelMap;

    for (UEdGraphNode* Node : Graph->Nodes)
    {
        UQuestlineNode_ContentBase* ContentNode = Cast<UQuestlineNode_ContentBase>(Node);
        if (!ContentNode) continue;
        OutContentNodes.Add(ContentNode);
    	
    	/**
		 * LinkedQuestline's GetNodeTitle is driven by the referenced asset's name (so multiple placements of the same asset share
		 * a title); use NodeLabel directly to guarantee per-placement uniqueness. Other content nodes' GetNodeTitle already reflects
		 * NodeLabel via the base ContentBase path, so behavior is unchanged for them.
		 */
    	const FString Label = Cast<UQuestlineNode_LinkedQuestline>(ContentNode)
			? SanitizeTagSegment(ContentNode->NodeLabel.ToString())
			: SanitizeTagSegment(ContentNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
    	
        if (Label.IsEmpty())
        {
            AddError(FString::Printf(TEXT("[%s] A content node has an empty label. All Quest and Step nodes must have a label before compiling."), *TagPrefix), ContentNode);
            continue;
        }
        if (LabelMap.Contains(Label))
        {
            AddError(FString::Printf(TEXT("[%s] Duplicate node label '%s'. Labels must be unique within a graph."), *TagPrefix, *Label), ContentNode);
            continue;
        }
        LabelMap.Add(Label, ContentNode);

        // Create the appropriate runtime instance
        UQuestNodeBase* Instance = nullptr;

        if (UQuestlineNode_Quest* QuestEdNode = Cast<UQuestlineNode_Quest>(ContentNode))
        {
            UQuest* QuestInstance = NewObject<UQuest>(RootGraph);
            if (QuestEdNode->GetInnerGraph())
            {
                const FString InnerPrefix = TagPrefix + TEXT(".") + Label;
                TMap<FGameplayTag, FQuestEntryRouteList> InnerEntryByOutcome;
                QuestInstance->EntryStepTags = CompileGraph(QuestEdNode->GetInnerGraph(), InnerPrefix, {}, VisitedAssetPaths, &InnerEntryByOutcome);
                QuestInstance->EntryStepTagsByOutcome = MoveTemp(InnerEntryByOutcome);
                
                // Register entry outcome fact tags for prerequisite expressions within the inner graph.
                for (UEdGraphNode* InnerNode : QuestEdNode->GetInnerGraph()->Nodes)
                {
                    if (UQuestlineNode_Entry* EntryNode = Cast<UQuestlineNode_Entry>(InnerNode))
                    {
                        const FName QuestTagName = MakeNodeTagName(TagPrefix, Label);
                        for (const FIncomingSignalPinSpec& Spec : EntryNode->IncomingSignals)
                        {
                            if (!Spec.bExposed) continue;
                            if (!Spec.Outcome.IsValid()) continue;
                            AllCompiledQuestTags.AddUnique(UQuestStateTagUtils::MakeEntryOutcomeFact(QuestTagName, Spec.Outcome));
                        }
                        break;
                    }
                }
            }            
            Instance = QuestInstance;
        }
        else if (UQuestlineNode_Step* StepNode = Cast<UQuestlineNode_Step>(ContentNode))
        {
            if (!StepNode->ObjectiveClass)
            {
                AddError(FString::Printf(TEXT("[%s] Step node '%s' has no Objective Class assigned."), *TagPrefix, *Label), ContentNode);
                continue;
            }
            UQuestStep* StepInstance = NewObject<UQuestStep>(RootGraph);
            StepInstance->QuestObjective = StepNode->ObjectiveClass;
            StepInstance->Reward = StepNode->RewardClass;
            StepInstance->TargetClasses = StepNode->TargetClasses;
            StepInstance->NumberOfElements = StepNode->NumberOfElements;
            StepInstance->TargetVector = StepNode->TargetVector;
            StepInstance->TargetActors.Append(StepNode->TargetActors);
            StepInstance->PrerequisiteGateMode = StepNode->PrerequisiteGateMode;
            Instance = StepInstance;
        }
		else if (UQuestlineNode_LinkedQuestline* LinkedNode = Cast<UQuestlineNode_LinkedQuestline>(ContentNode))
		{
			if (LinkedNode->LinkedGraph.IsNull())
			{
				AddWarning(FString::Printf(TEXT("[%s] LinkedQuestline node '%s' has no asset assigned — skipped."),
					*TagPrefix,
					*Label),
					LinkedNode);
				continue;
			}

			UQuestlineGraph* LinkedGraph = LinkedNode->LinkedGraph.LoadSynchronous();
			if (!LinkedGraph || !LinkedGraph->QuestlineEdGraph)
			{
				AddError(FString::Printf(TEXT("[%s] LinkedQuestline '%s' failed to load asset '%s'."),
					*TagPrefix,
					*Label,
					*LinkedNode->LinkedGraph.ToString()),
					LinkedNode);
				continue;
			}

			// Refresh outcome pins before reading them — the linked graph's Exit tags may have changed since this node was last
			// edited or loaded, without triggering PostLoad/PostEditChangeProperty on the parent. Runs once per compile, per
			// placement, which is cheap.
			LinkedNode->RebuildOutcomePinsFromLinkedGraph();


			const FString LinkedPath = LinkedGraph->GetPathName();
			if (VisitedAssetPaths.Contains(LinkedPath))
			{
				/**
				 * Reconstruct the cycle path for the error: slice VisitedAssetPaths from the cycling asset's prior entry to the end, then
				 * close with the cycling asset name again. The cycle is a property of the chain as a whole — this link is not uniquely at
				 * fault, it just happens to be the one that closes the loop during recursion. Message is worded to make that explicit so
				 * designers don't assume the highlighted link is the error.
				 */
				const int32 CycleStart = VisitedAssetPaths.IndexOfByKey(LinkedPath);
				FString CyclePath;
				for (int32 i = CycleStart; i < VisitedAssetPaths.Num(); ++i)
				{
					CyclePath += FPackageName::ObjectPathToObjectName(VisitedAssetPaths[i]);
					CyclePath += TEXT(" → ");
				}
				CyclePath += FPackageName::ObjectPathToObjectName(LinkedPath);

				AddError(FString::Printf(
					TEXT("LinkedQuestline cycle detected: compile chain [%s]. This link closes the cycle; it is valid in isolation, "
					"but any link in this chain must be removed for compilation to succeed. Use activation group setter/getter pairs for runtime "
					"loops across assets."),
					*CyclePath),
					LinkedNode);
				continue;
			}

			UQuest* QuestInstance = NewObject<UQuest>(RootGraph);

			/**
			 * Build the boundary map: each LinkedQuestline output pin represents an exit outcome of the linked asset, and its
			 * downstream wires in THIS (parent) graph are the destinations the linked Exit nodes should route to. Named outcome
			 * pins keyed by outcome tag; "Any Outcome" pin stored under the invalid tag as a catch-all.
			 */
			TMap<FGameplayTag, TArray<FName>> LinkedBoundaryByOutcome;
			for (UEdGraphPin* OutputPin : LinkedNode->Pins)
			{
				if (OutputPin->Direction != EGPD_Output) continue;

				TArray<FName> PinTags;
				ResolvePinToTags(OutputPin, TagPrefix, BoundaryTagsByOutcome, VisitedAssetPaths, PinTags);
				if (PinTags.IsEmpty()) continue;

				if (OutputPin->PinType.PinCategory == TEXT("QuestOutcome"))
				{
					const FGameplayTag OutcomeTag = UGameplayTagsManager::Get().RequestGameplayTag(OutputPin->PinName, false);
					if (OutcomeTag.IsValid()) for (const FName& Tag : PinTags) LinkedBoundaryByOutcome.FindOrAdd(OutcomeTag).AddUnique(Tag);
				}
				else if (OutputPin->PinName == TEXT("Any Outcome"))
				{
					for (const FName& Tag : PinTags) LinkedBoundaryByOutcome.FindOrAdd(FGameplayTag()).AddUnique(Tag);
				}
			}

			/**
			 * Compile the linked asset's graph as the UQuest's inner graph. TagPrefix for the inner compile is the LinkedQuestline's
			 * own compiled path — same pattern as inline Quest. The linked content nodes' compiled tags thus nest under this
			 * LinkedQuestline's tag, keeping a stable per-parent namespace when the same linked asset is referenced from multiple
			 * places.
			 */
			VisitedAssetPaths.Add(LinkedPath);

			/**
			 * Push the linked placement's GUID onto the chain so inner content nodes produce placement-unique compound GUIDs.
			 * Save/restore with local so nested LinkedQuestlines accumulate correctly through multiple levels.
			 */
			const FGuid PreviousGuidChain = CurrentOuterGuidChain;
			CurrentOuterGuidChain = CombineGuids(CurrentOuterGuidChain, LinkedNode->QuestGuid);

			const FString InnerPrefix = TagPrefix + TEXT(".") + Label;
			TMap<FGameplayTag, FQuestEntryRouteList> InnerEntryByOutcome;
			QuestInstance->EntryStepTags = CompileGraph(LinkedGraph->QuestlineEdGraph, InnerPrefix, LinkedBoundaryByOutcome, VisitedAssetPaths, &InnerEntryByOutcome);
			QuestInstance->EntryStepTagsByOutcome = MoveTemp(InnerEntryByOutcome);

			CurrentOuterGuidChain = PreviousGuidChain;
			VisitedAssetPaths.RemoveSingleSwap(LinkedPath);

			/**
			 * Register entry outcome fact tags for prerequisite expressions inside the linked graph — same pattern as the inline
			 * Quest branch. Iterates the linked graph's Entry node specs.
			 */
			for (UEdGraphNode* InnerNode : LinkedGraph->QuestlineEdGraph->Nodes)
			{
				if (UQuestlineNode_Entry* EntryNode = Cast<UQuestlineNode_Entry>(InnerNode))
				{
					const FName QuestTagName = MakeNodeTagName(TagPrefix, Label);
					for (const FIncomingSignalPinSpec& Spec : EntryNode->IncomingSignals)
					{
						if (!Spec.bExposed) continue;
						if (!Spec.Outcome.IsValid()) continue;
						AllCompiledQuestTags.AddUnique(UQuestStateTagUtils::MakeEntryOutcomeFact(QuestTagName, Spec.Outcome));
					}
					break;
				}
			}

			Instance = QuestInstance;
		}
        
        if (!Instance) continue;
        
    	Instance->QuestContentGuid = CombineGuids(CurrentOuterGuidChain, ContentNode->QuestGuid);
        Instance->NodeInfo.DisplayName = ContentNode->NodeLabel;
        const FName TagName = MakeNodeTagName(TagPrefix, Label);
        AllCompiledQuestTags.Add(TagName);

        // Register outcome tags: both the raw Quest.Outcome.* tag and the per-node fact tag
        if (const UQuestlineNode_Step* QuestStepNode = Cast<UQuestlineNode_Step>(ContentNode))
        {
            if (QuestStepNode->ObjectiveClass)
            {
                TArray<FGameplayTag> Outcomes = USimpleQuestEditorUtilities::DiscoverObjectiveOutcomes(QuestStepNode->ObjectiveClass);
                for (const FGameplayTag& OutcomeTag : Outcomes)
                {
                    AllCompiledQuestTags.AddUnique(OutcomeTag.GetTagName());
                    AllCompiledQuestTags.AddUnique(UQuestStateTagUtils::MakeNodeOutcomeFact(TagName, OutcomeTag));
                }
            }
        }
        
        AllCompiledNodes.Add(TagName, Instance);
        AllCompiledEditorNodes.Add(TagName, ContentNode);
        OutNodeInstanceMap.Add(ContentNode, Instance);
    }
}

void FQuestlineGraphCompiler::CompileGroupSetters(UEdGraph* Graph, const FString& TagPrefix, TArray<FName>& OutMonitorTags, TArray<FName>& OutGetterEntryTags)
{
    // ---- Prerequisite group setters: merge conditions per unique GroupTag ----
    TMap<FGameplayTag, TArray<UQuestlineNode_PrerequisiteGroupSetter*>> PrereqSettersByTag;

    for (UEdGraphNode* Node : Graph->Nodes)
    {
        if (UQuestlineNode_PrerequisiteGroupSetter* Setter = Cast<UQuestlineNode_PrerequisiteGroupSetter>(Node))
        {
            if (!Setter->GroupTag.IsValid())
            {
                AddWarning(FString::Printf(TEXT("[%s] A Prereq Group Setter has no GroupTag set and will be skipped."), *TagPrefix), Setter);
                continue;
            }
            PrereqSettersByTag.FindOrAdd(Setter->GroupTag).Add(Setter);
        }
    }

    for (auto& [GroupTag, Setters] : PrereqSettersByTag)
    {
        UQuestPrereqGroupNode* Monitor = NewObject<UQuestPrereqGroupNode>(RootGraph);
        Monitor->GroupTag = GroupTag;

        for (UQuestlineNode_PrerequisiteGroupSetter* Setter : Setters)
        {
            for (UEdGraphPin* Pin : Setter->Pins)
            {
                if (Pin->Direction != EGPD_Input) continue;
                if (!Pin->PinName.ToString().StartsWith(TEXT("Condition"))) continue;

                for (UEdGraphPin* LinkedOutputPin : Pin->LinkedTo)
                {
                    const FName FactTagName = ResolveOutputPinToStateFact(LinkedOutputPin, TagPrefix);
                    if (FactTagName.IsNone()) continue;

                    const FGameplayTag FactTag = UGameplayTagsManager::Get().RequestGameplayTag(FactTagName, false);
                    if (FactTag.IsValid())
                    {
                        Monitor->ConditionTags.AddUnique(FactTag);
                    }
                }
            }
        }

        const FName GroupTagName = GroupTag.GetTagName();
        AllCompiledNodes.Add(GroupTagName, Monitor);
        AllCompiledQuestTags.Add(GroupTagName);
        OutMonitorTags.Add(GroupTagName);

        UE_LOG(LogSimpleQuest, Verbose, TEXT("CompileGroupSetters: [%s] prereq group '%s' — %d condition(s) from %d setter(s)"),
            *TagPrefix, *GroupTagName.ToString(), Monitor->ConditionTags.Num(), Setters.Num());
    }

    // ---- Activation group setters ----
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        UQuestlineNode_ActivationGroupSetter* Setter = Cast<UQuestlineNode_ActivationGroupSetter>(Node);
        if (!Setter) continue;

        if (!Setter->GroupTag.IsValid())
        {
            AddWarning(FString::Printf(TEXT("[%s] An Activation Group Setter has no GroupTag set and will be skipped."), *TagPrefix), Setter);
            continue;
        }

        UActivationGroupSetterNode* Inst = NewObject<UActivationGroupSetterNode>(RootGraph);
        Inst->GroupTag = Setter->GroupTag;

        const FName UtilKey = FName(*FString::Printf(TEXT("Util_%s"), *Node->NodeGuid.ToString()));
        UtilityNodeKeyMap.Add(Node, UtilKey);
        AllCompiledNodes.Add(UtilKey, Inst);
        AllCompiledEditorNodes.Add(UtilKey, Node);

        UE_LOG(LogSimpleQuest, Verbose, TEXT("CompileGroupSetters: [%s] activation setter '%s'"),
            *TagPrefix, *Setter->GroupTag.GetTagName().ToString());
    }

    // ---- Activation group getters (source nodes — add to entry tags) ----
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        UQuestlineNode_ActivationGroupGetter* Getter = Cast<UQuestlineNode_ActivationGroupGetter>(Node);
        if (!Getter) continue;

        if (!Getter->GroupTag.IsValid())
        {
            AddWarning(FString::Printf(TEXT("[%s] An Activation Group Getter has no GroupTag set and will be skipped."), *TagPrefix), Getter);
            continue;
        }

        UActivationGroupGetterNode* Inst = NewObject<UActivationGroupGetterNode>(RootGraph);
        Inst->GroupTag = Getter->GroupTag;

        const FName UtilKey = FName(*FString::Printf(TEXT("Util_%s"), *Node->NodeGuid.ToString()));
        UtilityNodeKeyMap.Add(Node, UtilKey);
        AllCompiledNodes.Add(UtilKey, Inst);
        AllCompiledEditorNodes.Add(UtilKey, Node);
        OutGetterEntryTags.Add(UtilKey);

        UE_LOG(LogSimpleQuest, Verbose, TEXT("CompileGroupSetters: [%s] activation getter '%s' (entry tag)"),
            *TagPrefix, *Getter->GroupTag.GetTagName().ToString());
    }
}

void FQuestlineGraphCompiler::CompileUtilityNodes(UEdGraph* Graph, TArray<UQuestlineNode_UtilityBase*>& OutUtilityEdNodes)
{
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        UQuestlineNode_UtilityBase* UtilEdNode = Cast<UQuestlineNode_UtilityBase>(Node);
        if (!UtilEdNode) continue;

        UQuestNodeBase* Instance = nullptr;

        if (UQuestlineNode_SetBlocked* BlockNode = Cast<UQuestlineNode_SetBlocked>(UtilEdNode))
        {
            USetBlockedNode* Inst = NewObject<USetBlockedNode>(RootGraph);
            Inst->TargetQuestTags = BlockNode->TargetQuestTags;
            Instance = Inst;
        }
        else if (UQuestlineNode_ClearBlocked* ClearBlockNode = Cast<UQuestlineNode_ClearBlocked>(UtilEdNode))
        {
            UClearBlockedNode* Inst = NewObject<UClearBlockedNode>(RootGraph);
            Inst->TargetQuestTags = ClearBlockNode->TargetQuestTags;
            Instance = Inst;
        }

        if (!Instance) continue;

        const FName UtilKey = FName(*FString::Printf(TEXT("Util_%s"), *Node->NodeGuid.ToString()));
        OutUtilityEdNodes.Add(UtilEdNode);
        UtilityNodeKeyMap.Add(Node, UtilKey);
        AllCompiledNodes.Add(UtilKey, Instance);
        AllCompiledEditorNodes.Add(UtilKey, Node);
    }
}

void FQuestlineGraphCompiler::CompileOutputWiring(const TArray<UQuestlineNode_ContentBase*>& ContentNodes, const TMap<UQuestlineNode_ContentBase*, UQuestNodeBase*>& NodeInstanceMap, const FString& TagPrefix, const TMap<FGameplayTag, TArray<FName>>& BoundaryTagsByOutcome, TArray<FString>& VisitedAssetPaths)
{
    for (UQuestlineNode_ContentBase* ContentNode : ContentNodes)
    {
        if (Cast<UQuestlineNode_LinkedQuestline>(ContentNode)) continue;

        UQuestNodeBase* Instance = NodeInstanceMap.FindRef(ContentNode);
        if (!Instance) continue;

        Instance->NextNodesByOutcome.Empty();
        Instance->NextNodesOnAnyOutcome.Empty();
        Instance->NextNodesOnDeactivation.Empty();
        Instance->NextNodesToDeactivateOnDeactivation.Empty();

        // Route each output pin into the correct runtime routing set
        for (UEdGraphPin* Pin : ContentNode->Pins)
        {
            if (Pin->Direction != EGPD_Output) continue;
            if (Pin->bOrphanedPin) continue; 

            // Deactivated pin: split routing by destination pin category
            if (Pin->PinType.PinCategory == TEXT("QuestDeactivated"))
            {
                TArray<FName> ActivateTags, DeactivateTags;
                ResolveDeactivatedPinToTags(Pin, TagPrefix, VisitedAssetPaths, ActivateTags, DeactivateTags);
                for (const FName& Tag : ActivateTags)  Instance->NextNodesOnDeactivation.Add(Tag);
                for (const FName& Tag : DeactivateTags) Instance->NextNodesToDeactivateOnDeactivation.Add(Tag);
                continue;
            }

            TArray<FName> ResolvedTags;
            ResolvePinToTags(Pin, TagPrefix, BoundaryTagsByOutcome, VisitedAssetPaths, ResolvedTags);
            if (ResolvedTags.IsEmpty()) continue;

            if (Pin->PinType.PinCategory == TEXT("QuestOutcome"))
            {
                const FGameplayTag OutcomeTag = UGameplayTagsManager::Get().RequestGameplayTag(Pin->PinName, false);
                if (OutcomeTag.IsValid())
                {
                    FQuestOutcomeNodeList& List = Instance->NextNodesByOutcome.FindOrAdd(OutcomeTag);
                    for (const FName& Tag : ResolvedTags) List.NodeTags.AddUnique(Tag);
                }
            }
            else if (Pin->PinName == TEXT("Any Outcome"))
            {
                for (const FName& Tag : ResolvedTags) Instance->NextNodesOnAnyOutcome.Add(Tag);
            }
        }

        // Entry Deactivated pin: merge inner Entry node's deactivation routing into this Quest instance
        if (UQuestlineNode_Quest* QuestEdNode = Cast<UQuestlineNode_Quest>(ContentNode))
        {
            if (UEdGraph* InnerGraph = QuestEdNode->GetInnerGraph())
            {
                for (UEdGraphNode* InnerNode : InnerGraph->Nodes)
                {
                    if (UQuestlineNode_Entry* EntryNode = Cast<UQuestlineNode_Entry>(InnerNode))
                    {
                        if (UEdGraphPin* DeactivatedPin = EntryNode->FindPin(TEXT("Deactivated"), EGPD_Output))
                        {
                            if (!DeactivatedPin->bOrphanedPin && DeactivatedPin->LinkedTo.Num() > 0)
                            {
                                const FString Label = SanitizeTagSegment(ContentNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
                                const FString InnerPrefix = TagPrefix + TEXT(".") + Label;
                                TArray<FName> ActivateTags, DeactivateTags;
                                ResolveDeactivatedPinToTags(DeactivatedPin, InnerPrefix, VisitedAssetPaths, ActivateTags, DeactivateTags);
                                for (const FName& Tag : ActivateTags)  Instance->NextNodesOnDeactivation.Add(Tag);
                                for (const FName& Tag : DeactivateTags) Instance->NextNodesToDeactivateOnDeactivation.Add(Tag);
                            }
                        }
                        break;
                    }
                }
            }
        }
        
        // Mark nodes whose output chain reaches an exit — they complete their parent graph
        {
            auto CheckExit = [this](UEdGraphPin* Pin) -> bool
            {
                TSet<const UEdGraphNode*> V;
                return TraversalPolicy->HasDownstreamExit(Pin, V);
            };
            bool bCompletesParent = false;
            for (UEdGraphPin* Pin : ContentNode->Pins)
            {
                if (Pin->Direction == EGPD_Output && !Pin->bOrphanedPin && CheckExit(Pin))
                {
                    bCompletesParent = true;
                    break;
                }
            }
            Instance->bCompletesParentGraph = bCompletesParent;
        }
        
        if (UEdGraphPin* PrereqPin = ContentNode->FindPin(TEXT("Prerequisites"), EGPD_Input))
        {
            if (PrereqPin->LinkedTo.Num() > 0)
            {
                Instance->PrerequisiteExpression = CompilePrerequisiteExpression(PrereqPin, TagPrefix, VisitedAssetPaths);
            }
        }
    }
}

TArray<FName> FQuestlineGraphCompiler::ResolveEntryTags(UEdGraph* Graph, const FString& TagPrefix, const TMap<FGameplayTag, TArray<FName>>& BoundaryTagsByOutcome, TArray<FString>& VisitedAssetPaths, TMap<FGameplayTag, FQuestEntryRouteList>* OutEntryTagsByOutcome)
{
	TArray<FName> EntryTags;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		UQuestlineNode_Entry* EntryNode = Cast<UQuestlineNode_Entry>(Node);
		if (!EntryNode) continue;

		/**
		 * Non-outcome output pins (Any Outcome, Deactivated) produce unconditional entry tags — no per-outcome or per-source
		 * routing, just "fire when this graph enters regardless of context."
		 */
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin->Direction != EGPD_Output) continue;
			if (Pin->bOrphanedPin) continue;
			if (Pin->PinType.PinCategory == TEXT("QuestOutcome")) continue;
			ResolvePinToTags(Pin, TagPrefix, BoundaryTagsByOutcome, VisitedAssetPaths, EntryTags);
		}

		/**
		 * Per-spec routing for QuestOutcome pins. Iterate IncomingSignals directly — pin names are disambiguated and not
		 * parseable as gameplay tags. Each exposed spec produces one FQuestEntryDestination per resolved downstream tag, each
		 * tagged with the compiled QuestTag of the source content node as SourceFilter.
		 */
		if (OutEntryTagsByOutcome)
		{
			const UQuestlineGraph* ChildAsset = FQuestlineGraphTraversalPolicy::ResolveContainingAsset(Graph);
			for (const FIncomingSignalPinSpec& Spec : EntryNode->IncomingSignals)
			{
				if (!Spec.bExposed) continue;
				if (!Spec.Outcome.IsValid()) continue;
				if (!Spec.SourceNodeGuid.IsValid())
				{
					AddWarning(FString::Printf(TEXT("[%s] Entry has unqualified incoming-signal spec for outcome '%s' — skipped. Re-run Import."), *TagPrefix, *Spec.Outcome.ToString()), EntryNode);
					continue;
				}

				const FName PinName = UQuestlineNode_Entry::BuildDisambiguatedPinName(Spec, EntryNode->IncomingSignals);
				UEdGraphPin* SpecPin = EntryNode->FindPin(PinName, EGPD_Output);
				if (!SpecPin)
				{
					AddWarning(FString::Printf(TEXT("[%s] Entry spec for outcome '%s' has no corresponding pin '%s' — skipped."), *TagPrefix, *Spec.Outcome.ToString(), *PinName.ToString()), EntryNode);
					continue;
				}

				const FName SourceFilter = ResolveSourceFilterTag(Spec, ChildAsset);
				if (SourceFilter.IsNone())
				{
					AddWarning(FString::Printf(TEXT("[%s] Entry spec for outcome '%s' has unresolvable source (GUID %s) — skipped. Re-run Import to refresh, or verify the parent asset is accessible."), *TagPrefix, *Spec.Outcome.ToString(), *Spec.SourceNodeGuid.ToString()), EntryNode);
					continue;
				}

				TArray<FName> DestTags;
				ResolvePinToTags(SpecPin, TagPrefix, BoundaryTagsByOutcome, VisitedAssetPaths, DestTags);

				FQuestEntryRouteList& RouteList = OutEntryTagsByOutcome->FindOrAdd(Spec.Outcome);
				for (const FName& DestTag : DestTags)
				{
					FQuestEntryDestination Dest;
					Dest.DestTag = DestTag;
					Dest.SourceFilter = SourceFilter;
					RouteList.Destinations.Add(Dest);
				}
			}
		}

		break;
	}
	return EntryTags;
}

void FQuestlineGraphCompiler::DetectAndRecordTagRenames(UQuestlineGraph* InGraph, const TMap<FGuid, FName>& OldTagsByGuid)
{
    for (const auto& [TagName, NodeInstance] : InGraph->CompiledNodes)
    {
        if (!NodeInstance || !NodeInstance->GetQuestGuid().IsValid()) continue;
        if (const FName* OldTag = OldTagsByGuid.Find(NodeInstance->GetQuestGuid()))
        {
            if (*OldTag != TagName)
            {
                DetectedTagRenames.Add(*OldTag, TagName);
            }
        }
    }

    if (DetectedTagRenames.Num() == 0) return;

    // Chain-collapse the persistent ledger
    for (FQuestTagRename& Existing : InGraph->PendingTagRenames)
    {
        if (const FName* ChainedNew = DetectedTagRenames.Find(Existing.NewTag))
        {
            Existing.NewTag = *ChainedNew;
        }
    }

    // Add new entries not already covered by chain collapse
    TSet<FName> ExistingOldTags;
    for (const FQuestTagRename& Existing : InGraph->PendingTagRenames)
    {
        ExistingOldTags.Add(Existing.OldTag);
    }
    for (const auto& [OldTag, NewTag] : DetectedTagRenames)
    {
        if (!ExistingOldTags.Contains(OldTag))
        {
            InGraph->PendingTagRenames.Add({ OldTag, NewTag });
        }
    }

    // Prune identity entries (rename then rename back)
    InGraph->PendingTagRenames.RemoveAll([](const FQuestTagRename& E)
    {
        return E.OldTag == E.NewTag;
    });
	UE_LOG(LogSimpleQuest, Display, TEXT("Compiler: %d tag rename(s) detected, ledger: %d pending"),
		DetectedTagRenames.Num(), InGraph->PendingTagRenames.Num());

	/**
	 * Per-rename detail — walks the new CompiledNodes to recover the GUID and DisplayName of each renamed node so the
	 * Output Log identifies exactly which node is drifting. Intended for diagnosing stale or persistent renames where
	 * the same tag flips every compile without a designer-visible reason.
	 */
	for (const auto& [OldTag, NewTag] : DetectedTagRenames)
	{
		FGuid OffendingGuid;
		FText OffendingDisplayName;
		if (TObjectPtr<UQuestNodeBase>* Found = InGraph->CompiledNodes.Find(NewTag))
		{
			if (UQuestNodeBase* Node = *Found)
			{
				OffendingGuid = Node->GetQuestGuid();
				OffendingDisplayName = Node->GetNodeInfo().DisplayName;
			}
		}
		UE_LOG(LogSimpleQuest, Display, TEXT("  rename: '%s' -> '%s' (node '%s', GUID %s)"),
			*OldTag.ToString(),
			*NewTag.ToString(),
			*OffendingDisplayName.ToString(),
			*OffendingGuid.ToString(EGuidFormats::Digits));
	}
}


// -------------------------------------------------------------------------------------------------
// ResolvePinToTags - the node traversal engine
// -------------------------------------------------------------------------------------------------

void FQuestlineGraphCompiler::ResolvePinToTags(UEdGraphPin* FromPin, const FString& TagPrefix, const TMap<FGameplayTag, TArray<FName>>& BoundaryTagsByOutcome, TArray<FString>& VisitedAssetPaths, TArray<FName>& OutTags)
{
    for (UEdGraphPin* LinkedPin : FromPin->LinkedTo)
    {
        UEdGraphNode* Node = LinkedPin->GetOwningNode();

        // Knot: pass through to the other side
        if (UQuestlineNode_Knot* Knot = Cast<UQuestlineNode_Knot>(Node))
        {
            if (UEdGraphPin* KnotOut = Knot->FindPin(TEXT("KnotOut"), EGPD_Output))
            {
                ResolvePinToTags(KnotOut, TagPrefix, BoundaryTagsByOutcome, VisitedAssetPaths, OutTags);
            }
        }

        // Exit nodes: inject boundary tags for the parent graph so a child graph knows what its exits connect to on the parent level
        // Passed to children when compilation recurses into the child graph.
        else if (const UQuestlineNode_Exit* ExitNode = Cast<UQuestlineNode_Exit>(Node))
        {
            if (const TArray<FName>* BoundaryTags = BoundaryTagsByOutcome.Find(ExitNode->OutcomeTag))
            {
                for (const FName& Tag : *BoundaryTags) OutTags.AddUnique(Tag);
            }
            // Fall back to Any Outcome boundary (stored under invalid tag by linked node handler)
            else if (const TArray<FName>* AnyBoundaryTags = BoundaryTagsByOutcome.Find(FGameplayTag()))
            {
                for (const FName& Tag : *AnyBoundaryTags) OutTags.AddUnique(Tag);
            }
            else if (!ExitNode->OutcomeTag.IsValid())
            {
                AddWarning(FString::Printf(TEXT("[%s] An exit node has no OutcomeTag set."), *TagPrefix), ExitNode);
            }
        }

        // Quest or Step: return the tag assigned during Pass 1
        else if (UQuestlineNode_ContentBase* ContentNode = Cast<UQuestlineNode_ContentBase>(Node))
        {
            // Only resolve forward chain when wired to an Activate input. Prerequisite and Deactivate inputs are compiled
            // by their own dedicated passes.
            if (LinkedPin->PinType.PinCategory != TEXT("QuestActivation"))
                continue;

            const FString Label = SanitizeTagSegment(ContentNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
            if (!Label.IsEmpty())
            {
                const FName TagName = MakeNodeTagName(TagPrefix, Label);
                if (!TagName.IsNone())
                {
                    OutTags.AddUnique(TagName);
                }
            }
        }
        
        // Utility node: return its GUID-based key so the caller can route into NextNodesOnForward
        else if (const FName* UtilKey = UtilityNodeKeyMap.Find(Node))
        {
            OutTags.AddUnique(*UtilKey);
        }
    }
}


// -------------------------------------------------------------------------------------------------
// Helpers
// -------------------------------------------------------------------------------------------------

FString FQuestlineGraphCompiler::SanitizeTagSegment(const FString& InLabel) const
{
    return USimpleQuestEditorUtilities::SanitizeQuestlineTagSegment(InLabel);
}

FName FQuestlineGraphCompiler::MakeNodeTagName(const FString& TagPrefix, const FString& SanitizedLabel) const
{
    return FName(*FString::Printf(TEXT("Quest.%s.%s"), *TagPrefix, *SanitizedLabel));
}

void FQuestlineGraphCompiler::AddError(const FString& Message, const UEdGraphNode* Node)
{
    bHasErrors = true;
    NumErrors++;
    TSharedRef<FTokenizedMessage> Msg = FTokenizedMessage::Create(EMessageSeverity::Error, FText::FromString(Message));
    if (Node) AddNodeNavigationToken(Msg, Node);
    Messages.Add(Msg);
    UE_LOG(LogSimpleQuest, Error, TEXT("QuestlineGraphCompiler: %s"), *Message);
}

void FQuestlineGraphCompiler::AddWarning(const FString& Message, const UEdGraphNode* Node)
{
    NumWarnings++;
    TSharedRef<FTokenizedMessage> Msg = FTokenizedMessage::Create(EMessageSeverity::Warning, FText::FromString(Message));
    if (Node) AddNodeNavigationToken(Msg, Node);
    Messages.Add(Msg);
    UE_LOG(LogSimpleQuest, Warning, TEXT("QuestlineGraphCompiler: %s"), *Message);
}

void FQuestlineGraphCompiler::RegisterCompiledTags(UQuestlineGraph* InGraph)
{
    ISimpleQuestEditorModule::Get().RegisterCompiledTags(
        InGraph->GetPackage()->GetName(),
        InGraph->CompiledQuestTags);
}

FName FQuestlineGraphCompiler::ComputeCompiledTagForContentNode(const UQuestlineNode_ContentBase* SourceNode, const UQuestlineGraph* ContainingAsset) const
{
	if (!SourceNode || !ContainingAsset) return NAME_None;

	/**
	 * Walk up the Outer chain collecting sanitized labels. A content node either lives directly in the top-level asset graph
	 * or is nested inside one or more Quest node inner graphs. Each level contributes its label to the compiled tag path.
	 */
	TArray<FString> LabelsTopDown;
	const UEdGraphNode* Cursor = SourceNode;
	while (Cursor)
	{
		const UQuestlineNode_ContentBase* CursorContent = Cast<UQuestlineNode_ContentBase>(Cursor);
		if (!CursorContent) break;
		LabelsTopDown.Insert(SanitizeTagSegment(CursorContent->NodeLabel.ToString()), 0);

		const UEdGraph* Graph = Cursor->GetGraph();
		if (!Graph) break;
		UObject* Outer = Graph->GetOuter();
		if (const UQuestlineNode_Quest* ContainerQuest = Cast<UQuestlineNode_Quest>(Outer))
		{
			Cursor = ContainerQuest;
			continue;
		}
		break; // Reached the top-level asset graph.
	}

	if (LabelsTopDown.IsEmpty()) return NAME_None;

	const FString AssetPrefix = SanitizeTagSegment(ContainingAsset->GetQuestlineID().IsEmpty() ? ContainingAsset->GetName() : ContainingAsset->GetQuestlineID());
	FString FullPath = TEXT("Quest.") + AssetPrefix;
	for (const FString& Segment : LabelsTopDown) FullPath += TEXT(".") + Segment;
	return FName(*FullPath);
}

FName FQuestlineGraphCompiler::ResolveSourceFilterTag(const FIncomingSignalPinSpec& Spec, const UQuestlineGraph* ChildAsset) const
{
	if (!Spec.SourceNodeGuid.IsValid()) return NAME_None;

	/**
	 * Determine which asset contains the source. Same-asset (empty ParentAsset) uses ChildAsset; cross-asset sync-loads the
	 * referenced asset. Then recursively search all graphs in that asset for a content node with matching QuestGuid.
	 */
	UQuestlineGraph* SourceAsset = nullptr;
	if (Spec.ParentAsset.IsNull())
	{
		SourceAsset = const_cast<UQuestlineGraph*>(ChildAsset);
	}
	else
	{
		SourceAsset = Cast<UQuestlineGraph>(Spec.ParentAsset.TryLoad());
	}
	if (!SourceAsset || !SourceAsset->QuestlineEdGraph) return NAME_None;

	TFunction<const UQuestlineNode_ContentBase*(const UEdGraph*)> FindByGuid;
	FindByGuid = [&FindByGuid, &Spec](const UEdGraph* Graph) -> const UQuestlineNode_ContentBase*
	{
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (const UQuestlineNode_ContentBase* ContentNode = Cast<UQuestlineNode_ContentBase>(Node))
			{
				if (ContentNode->QuestGuid == Spec.SourceNodeGuid) return ContentNode;
			}
			if (const UQuestlineNode_Quest* QuestNode = Cast<UQuestlineNode_Quest>(Node))
			{
				if (UEdGraph* InnerGraph = QuestNode->GetInnerGraph())
				{
					if (const UQuestlineNode_ContentBase* Found = FindByGuid(InnerGraph)) return Found;
				}
			}
		}
		return nullptr;
	};

	const UQuestlineNode_ContentBase* SourceNode = FindByGuid(SourceAsset->QuestlineEdGraph);
	if (!SourceNode) return NAME_None;

	return ComputeCompiledTagForContentNode(SourceNode, SourceAsset);
}

int32 FQuestlineGraphCompiler::CompilePrerequisiteFromOutputPin(UEdGraphPin* OutputPin, const FString& TagPrefix, TArray<FString>& VisitedAssetPaths, FPrerequisiteExpression& OutExpression)
{
    if (!OutputPin) return INDEX_NONE;
    UEdGraphNode* Node = OutputPin->GetOwningNode();

    if (UQuestlineNode_Knot* Knot = Cast<UQuestlineNode_Knot>(Node))
    {
        UEdGraphPin* KnotIn = Knot->FindPin(TEXT("KnotIn"), EGPD_Input);
        if (KnotIn && KnotIn->LinkedTo.Num() > 0)
        {
            return CompilePrerequisiteFromOutputPin(KnotIn->LinkedTo[0], TagPrefix, VisitedAssetPaths, OutExpression);
        }
        return INDEX_NONE;
    }
    
    // AND
    if (Cast<UQuestlineNode_PrerequisiteAnd>(Node))
    {
        return CompileCombinatorNode(EPrerequisiteExpressionType::And, Node, TagPrefix, VisitedAssetPaths, OutExpression);
    }

    // OR
    if (Cast<UQuestlineNode_PrerequisiteOr>(Node))
    {
        return CompileCombinatorNode(EPrerequisiteExpressionType::Or, Node, TagPrefix, VisitedAssetPaths, OutExpression);
    }
    
    // NOT
    if (Cast<UQuestlineNode_PrerequisiteNot>(Node))
    {
        FPrerequisiteExpressionNode ExprNode;
        ExprNode.Type = EPrerequisiteExpressionType::Not;
        const int32 NodeIndex = OutExpression.Nodes.Add(ExprNode);

        if (UEdGraphPin* CondPin = Node->FindPin(TEXT("Condition_0"), EGPD_Input))
        {
            if (CondPin->LinkedTo.Num() > 0)
            {
                const int32 ChildIndex = CompilePrerequisiteFromOutputPin(CondPin->LinkedTo[0], TagPrefix, VisitedAssetPaths, OutExpression);
                if (ChildIndex != INDEX_NONE)
                {
                    OutExpression.Nodes[NodeIndex].ChildIndices.Add(ChildIndex);
                }
            }
        }
        return NodeIndex;
    }

    // Getter: resolves to a Leaf on the group's Satisfied tag
    if (UQuestlineNode_PrerequisiteGroupGetter* Getter = Cast<UQuestlineNode_PrerequisiteGroupGetter>(Node))
    {
        if (!Getter->GroupTag.IsValid())
        {
            AddWarning(FString::Printf(TEXT("[%s] A Prereq Group Getter has no GroupTag set and will be skipped."), *TagPrefix), Getter);
            return INDEX_NONE;
        }
        FPrerequisiteExpressionNode LeafNode;
        LeafNode.Type    = EPrerequisiteExpressionType::Leaf;
        LeafNode.LeafTag = Getter->GroupTag;
        return OutExpression.Nodes.Add(LeafNode);
    }

    // Setter PrereqOut: same leaf as getter — resolves to the group's tag
    if (UQuestlineNode_PrerequisiteGroupSetter* Setter = Cast<UQuestlineNode_PrerequisiteGroupSetter>(Node))
    {
        if (!Setter->GroupTag.IsValid())
        {
            AddWarning(FString::Printf(TEXT("[%s] A Prereq Group Setter has no GroupTag set and will be skipped."), *TagPrefix), Setter);
            return INDEX_NONE;
        }
        FPrerequisiteExpressionNode LeafNode;
        LeafNode.Type    = EPrerequisiteExpressionType::Leaf;
        LeafNode.LeafTag = Setter->GroupTag;
        return OutExpression.Nodes.Add(LeafNode);
    }
    
    // Entry node: outcome pin → leaf checking entry outcome fact; "Any Outcome" → parent quest Active fact
    if (Cast<UQuestlineNode_Entry>(Node))
    {
        const FName QuestTagName = FName(*(TEXT("Quest.") + TagPrefix));

        if (OutputPin->PinName == TEXT("Any Outcome"))
        {
            // The parent quest's Active fact is always set when the inner graph is running
            FPrerequisiteExpressionNode LeafNode;
            LeafNode.Type = EPrerequisiteExpressionType::Leaf;
            LeafNode.LeafTag = UGameplayTagsManager::Get().RequestGameplayTag(UQuestStateTagUtils::MakeStateFact(QuestTagName, UQuestStateTagUtils::Leaf_Active), false);
            return OutExpression.Nodes.Add(LeafNode);
        }

        const FGameplayTag OutcomeTag = UGameplayTagsManager::Get().RequestGameplayTag(OutputPin->PinName, false);
        if (!OutcomeTag.IsValid())
        {
            AddWarning(FString::Printf(TEXT("[%s] Entry outcome pin '%s' does not resolve to a valid gameplay tag — prerequisite skipped."),
                                       *TagPrefix, *OutputPin->PinName.ToString()), Node);
            return INDEX_NONE;
        }

        // Warn if used in a top-level graph — entry outcome facts are only written when a Quest node receives an IncomingOutcomeTag
        UObject* GraphOuter = Node->GetGraph() ? Node->GetGraph()->GetOuter() : nullptr;
        if (!Cast<UQuestlineNode_Quest>(GraphOuter))
        {
            AddWarning(FString::Printf(TEXT("[%s] Entry outcome '%s' used as prerequisite in a top-level graph — this fact is only set when a parent Quest node is activated with a matching outcome."),
                                       *TagPrefix, *OutcomeTag.ToString()), Node);
        }

        FPrerequisiteExpressionNode LeafNode;
        LeafNode.Type = EPrerequisiteExpressionType::Leaf;
        LeafNode.LeafTag = UGameplayTagsManager::Get().RequestGameplayTag(
            UQuestStateTagUtils::MakeEntryOutcomeFact(QuestTagName, OutcomeTag), false);
        return OutExpression.Nodes.Add(LeafNode);
    }

    
    // Content node: Success/Failure becomes single Leaf; Any Outcome builds OR(Succeeded, Failed)
    if (Cast<UQuestlineNode_ContentBase>(Node))
    {
        if (OutputPin->PinName == TEXT("Any Outcome"))
        {
            const UQuestlineNode_ContentBase* CN = Cast<UQuestlineNode_ContentBase>(Node);
            const FString Label = SanitizeTagSegment(CN->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
            const FName NodeTagName = MakeNodeTagName(TagPrefix, Label);

            // Collect all named QuestOutcome pins on this node
            TArray<UEdGraphPin*> OutcomePins;
            for (UEdGraphPin* Pin : Node->Pins)
            {
                if (Pin->Direction == EGPD_Output && Pin->PinType.PinCategory == TEXT("QuestOutcome"))
                    OutcomePins.Add(Pin);
            }

            // No named outcomes — this node is satisfied by Leaf_Completed alone
            if (OutcomePins.IsEmpty())
            {
                FPrerequisiteExpressionNode LeafNode;
                LeafNode.Type    = EPrerequisiteExpressionType::Leaf;
                LeafNode.LeafTag = UGameplayTagsManager::Get().RequestGameplayTag(
                    UQuestStateTagUtils::MakeStateFact(NodeTagName, UQuestStateTagUtils::Leaf_Completed), false);
                return OutExpression.Nodes.Add(LeafNode);
            }

            // Build OR over all named outcome facts
            FPrerequisiteExpressionNode OrNode;
            OrNode.Type = EPrerequisiteExpressionType::Or;
            const int32 OrIndex = OutExpression.Nodes.Add(OrNode);

            for (UEdGraphPin* OutcomePin : OutcomePins)
            {
                const FGameplayTag OutcomeTag = UGameplayTagsManager::Get().RequestGameplayTag(OutcomePin->PinName, false);
                if (!OutcomeTag.IsValid()) continue;

                FPrerequisiteExpressionNode LeafNode;
                LeafNode.Type = EPrerequisiteExpressionType::Leaf;
                LeafNode.LeafTag = UGameplayTagsManager::Get().RequestGameplayTag(UQuestStateTagUtils::MakeNodeOutcomeFact(NodeTagName, OutcomeTag), false);
                OutExpression.Nodes[OrIndex].ChildIndices.Add(OutExpression.Nodes.Add(LeafNode));
            }

            return OrIndex;
        }

        const FName FactTagName = ResolveOutputPinToStateFact(OutputPin, TagPrefix);
        if (FactTagName.IsNone()) return INDEX_NONE;

        FPrerequisiteExpressionNode LeafNode;
        LeafNode.Type    = EPrerequisiteExpressionType::Leaf;
        LeafNode.LeafTag = UGameplayTagsManager::Get().RequestGameplayTag(FactTagName, false);
        return OutExpression.Nodes.Add(LeafNode);
    }

    return INDEX_NONE;
}

int32 FQuestlineGraphCompiler::CompileCombinatorNode(EPrerequisiteExpressionType Type, UEdGraphNode* Node, const FString& TagPrefix, TArray<FString>& VisitedAssetPaths, FPrerequisiteExpression& OutExpression)
{
    FPrerequisiteExpressionNode ExprNode;
    ExprNode.Type = Type;
    const int32 NodeIndex = OutExpression.Nodes.Add(ExprNode);

    for (UEdGraphPin* Pin : Node->Pins)
    {
        if (Pin->Direction != EGPD_Input) continue;
        for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
        {
            const int32 ChildIndex = CompilePrerequisiteFromOutputPin(LinkedPin, TagPrefix, VisitedAssetPaths, OutExpression);
            if (ChildIndex != INDEX_NONE)
            {
                OutExpression.Nodes[NodeIndex].ChildIndices.Add(ChildIndex);
            }
        }
    }
    return NodeIndex;
}

FGuid FQuestlineGraphCompiler::CombineGuids(const FGuid& Outer, const FGuid& Inner)
{
	if (!Outer.IsValid()) return Inner;
	return FGuid(
		HashCombine(Outer.A, Inner.A),
		HashCombine(Outer.B, Inner.B),
		HashCombine(Outer.C, Inner.C),
		HashCombine(Outer.D, Inner.D));
}

FPrerequisiteExpression FQuestlineGraphCompiler::CompilePrerequisiteExpression(UEdGraphPin* PrerequisiteInputPin, const FString& TagPrefix, TArray<FString>& VisitedAssetPaths)
{
    FPrerequisiteExpression Expression;
    if (!PrerequisiteInputPin || PrerequisiteInputPin->LinkedTo.IsEmpty()) return Expression;

    // Schema enforces exactly one wire into any QuestPrerequisite input pin
    const int32 RootIndex = CompilePrerequisiteFromOutputPin(PrerequisiteInputPin->LinkedTo[0], TagPrefix, VisitedAssetPaths, Expression);

    if (RootIndex == INDEX_NONE)
    {
        Expression.Nodes.Reset(); // unresolvable — fall back to Always
    }
    else
    {
        Expression.RootIndex = RootIndex;
    }

    return Expression;
}

FName FQuestlineGraphCompiler::ResolveOutputPinToStateFact(
    UEdGraphPin* OutputPin, const FString& TagPrefix) const
{
    const UQuestlineNode_ContentBase* ContentNode = Cast<const UQuestlineNode_ContentBase>(OutputPin->GetOwningNode());
    if (!ContentNode) return NAME_None;

    const FString Label = SanitizeTagSegment(ContentNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
    if (Label.IsEmpty()) return NAME_None;

    const FName NodeTagName = MakeNodeTagName(TagPrefix, Label);
    const FName PinName = OutputPin->PinName;
    
    if (OutputPin->PinType.PinCategory == TEXT("QuestOutcome"))
    {
        const FGameplayTag OutcomeTag = UGameplayTagsManager::Get().RequestGameplayTag(PinName, false);
        if (OutcomeTag.IsValid())
        {
            return UQuestStateTagUtils::MakeNodeOutcomeFact(NodeTagName, OutcomeTag);
        }
    }
    return NAME_None; // Any Outcome or Abandon — caller handles these
}

void FQuestlineGraphCompiler::ResolveDeactivatedPinToTags(
    UEdGraphPin* FromPin, const FString& TagPrefix, TArray<FString>& VisitedAssetPaths,
    TArray<FName>& OutActivateTags, TArray<FName>& OutDeactivateTags)
{
    for (UEdGraphPin* LinkedPin : FromPin->LinkedTo)
    {
        UEdGraphNode* Node = LinkedPin->GetOwningNode();

        // Knot: pass through; the output side carries the category context to each destination
        if (UQuestlineNode_Knot* Knot = Cast<UQuestlineNode_Knot>(Node))
        {
            if (UEdGraphPin* KnotOut = Knot->FindPin(TEXT("KnotOut"), EGPD_Output))
            {
                ResolveDeactivatedPinToTags(KnotOut, TagPrefix, VisitedAssetPaths, OutActivateTags, OutDeactivateTags);
            }
            continue;
        }

        // Content node: classify by which input pin was connected
        if (UQuestlineNode_ContentBase* ContentNode = Cast<UQuestlineNode_ContentBase>(Node))
        {
            const FString Label = SanitizeTagSegment(ContentNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
            if (Label.IsEmpty()) continue;
            const FName TagName = MakeNodeTagName(TagPrefix, Label);
            if (TagName.IsNone()) continue;

            if (LinkedPin->PinType.PinCategory == TEXT("QuestActivation"))
            {
                // Deactivated to Activate: activate this node when the source deactivates
                OutActivateTags.AddUnique(TagName);
            }
            else if (LinkedPin->PinType.PinCategory == TEXT("QuestDeactivate"))
            {
                // Deactivated to Deactivate: cascade deactivation to this node
                OutDeactivateTags.AddUnique(TagName);
            }
            continue;
        }

        // Utility node: can only receive Activate, so always goes to OutActivateTags
        if (const FName* UtilKey = UtilityNodeKeyMap.Find(Node))
        {
            OutActivateTags.AddUnique(*UtilKey);
        }
    }
}

void FQuestlineGraphCompiler::AddNodeNavigationToken(TSharedRef<FTokenizedMessage>& Msg, const UEdGraphNode* Node)
{
    TWeakObjectPtr<UEdGraphNode> WeakNode = const_cast<UEdGraphNode*>(Node);

    Msg->AddToken(FActionToken::Create(
        FText::FromString(Node->GetNodeTitle(ENodeTitleType::ListView).ToString()),
        NSLOCTEXT("SimpleQuestEditor", "GoToNode", "Navigate to this node in the graph editor"),
        FOnActionTokenExecuted::CreateLambda([WeakNode]()
        {
            UEdGraphNode* PinnedNode = WeakNode.Get();
            if (!PinnedNode || !PinnedNode->GetGraph()) return;

            // Walk outer chain to find the UQuestlineGraph asset
            UQuestlineGraph* QuestlineGraph = nullptr;
            for (UObject* Outer = PinnedNode->GetGraph(); Outer; Outer = Outer->GetOuter())
            {
                QuestlineGraph = Cast<UQuestlineGraph>(Outer);
                if (QuestlineGraph) break;
            }
            if (!QuestlineGraph || !GEditor) return;

            // Open the asset editor and navigate to the node
            UAssetEditorSubsystem* EditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
            EditorSubsystem->OpenEditorForAsset(QuestlineGraph);

            if (IAssetEditorInstance* EditorInstance = EditorSubsystem->FindEditorForAsset(QuestlineGraph, false))
            {
                static_cast<FQuestlineGraphEditor*>(EditorInstance)->NavigateToLocation(PinnedNode->GetGraph(), PinnedNode);
            }
        })
    ));
}

