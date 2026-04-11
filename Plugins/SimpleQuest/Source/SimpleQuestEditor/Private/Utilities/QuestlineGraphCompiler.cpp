// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Utilities/QuestlineGraphCompiler.h"

#include "GameplayTagsManager.h"
#include "NativeGameplayTags.h"
#include "ISimpleQuestEditorModule.h"
#include "SimpleQuestEditor.h"
#include "SimpleQuestLog.h"
#include "Quests/QuestlineGraph.h"
#include "Quests/QuestNodeBase.h"
#include "Quests/QuestStep.h"
#include "Quests/Quest.h"
#include "Quests/PrerequisiteExpression.h"
#include "Quests/QuestPrereqGroupNode.h"
#include "Quests/SetBlockedNode.h"
#include "Quests/ClearBlockedNode.h"
#include "Quests/GroupSignalSetterNode.h"
#include "Quests/GroupSignalGetterNode.h"
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
#include "Nodes/Prerequisites/QuestlineNode_PrerequisiteGroupSetter.h"
#include "Nodes/Prerequisites/QuestlineNode_PrerequisiteGroupGetter.h"
#include "Nodes/Utility/QuestlineNode_SetBlocked.h"
#include "Nodes/Utility/QuestlineNode_ClearBlocked.h"
#include "Nodes/Groups/QuestlineNode_GroupSignalSetter.h"
#include "Nodes/Groups/QuestlineNode_GroupSignalGetter.h"
#include "Utilities/QuestStateTagUtils.h"
#include "Utilities/QuestlineGraphTraversalPolicy.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Objectives/QuestObjective.h"
#include "Rewards/QuestReward.h"
#include "Utilities/SimpleQuestEditorUtils.h"
#include "Utilities/QuestStateTagUtils.h"


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

    InGraph->Modify();
    InGraph->CompiledNodes.Empty(); 
    InGraph->EntryNodeTags.Empty();
    InGraph->CompiledQuestTags.Empty();
    AllCompiledNodes.Empty();
    UtilityNodeKeyMap.Empty();
    RootGraph = InGraph;

    // Refresh outcome pins on all step nodes so that changes to PossibleOutcomes on an objective class are reflected without
    // the designer having to touch ObjectiveClass again.
    for (UEdGraphNode* Node : InGraph->QuestlineEdGraph->Nodes)
    {
        if (UQuestlineNode_Step* StepNode = Cast<UQuestlineNode_Step>(Node)) StepNode->RefreshOutcomePins();
    }

    // The graphs that have already been compiled. Provided to CompileGraph, which forwards it to all recursive calls.
    TArray<FString> VisitedAssetPaths;
    VisitedAssetPaths.Add(InGraph->GetPathName());

    // Start recursive compilation, working forward from the Start node. This is the top level so there are no boundary tags to
    // pass in from a parent graph yet.
    TArray<FName> EntryTags = CompileGraph(InGraph->QuestlineEdGraph, TagPrefix, {}, VisitedAssetPaths);
    InGraph->EntryNodeTags = EntryTags;
    InGraph->CompiledNodes = MoveTemp(AllCompiledNodes);
    InGraph->CompiledEditorNodes = MoveTemp(AllCompiledEditorNodes);
    InGraph->CompiledQuestTags = MoveTemp(AllCompiledQuestTags);
    
    RegisterCompiledTags(InGraph);

    return !bHasErrors;
}


// -------------------------------------------------------------------------------------------------
// CompileGraph — recursive
// -------------------------------------------------------------------------------------------------

TArray<FName> FQuestlineGraphCompiler::CompileGraph(UEdGraph* Graph, const FString& TagPrefix, const TMap<FGameplayTag, TArray<FName>>& BoundaryTagsByOutcome,
                                                    TArray<FString>& VisitedAssetPaths)
{
    if (!Graph) return {};

    TArray<FName> MonitorTags;

    // ---- Pass 1: label uniqueness, GUID write, tag assignment ----
    // LinkedQuestline nodes are compiler-only scaffolding with no CDO; skip tag assignment

    TArray<UQuestlineNode_ContentBase*> ContentNodes;
    TMap<FString, UQuestlineNode_ContentBase*> LabelMap;
    TMap<UQuestlineNode_ContentBase*, UQuestNodeBase*> NodeInstanceMap;
    
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        UQuestlineNode_ContentBase* ContentNode = Cast<UQuestlineNode_ContentBase>(Node);
        if (!ContentNode) continue;
        ContentNodes.Add(ContentNode);
        const FString Label = SanitizeTagSegment(ContentNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString());

        // Linked questlines are erased from the runtime data set. Their connections are resolved and tags are written in-line
        // describing their context in the parent graph. 
        if (Cast<UQuestlineNode_LinkedQuestline>(ContentNode)) continue;

        if (Label.IsEmpty())
        {
            AddError(FString::Printf(TEXT("[%s] A content node has an empty label. All Quest and Step nodes must have a label before compiling."), *TagPrefix));
            continue;
        }
        if (LabelMap.Contains(Label))
        {
            AddError(FString::Printf(TEXT("[%s] Duplicate node label '%s'. Labels must be unique within a graph."), *TagPrefix, *Label));
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
                QuestInstance->EntryStepTags = CompileGraph(QuestEdNode->GetInnerGraph(), InnerPrefix, {}, VisitedAssetPaths);
            }            
            Instance = QuestInstance;
        }
        else if (UQuestlineNode_Step* StepNode = Cast<UQuestlineNode_Step>(ContentNode))
        {
            if (!StepNode->ObjectiveClass)
            {
                AddError(FString::Printf(TEXT("[%s] Step node '%s' has no Objective Class assigned."), *TagPrefix, *Label));
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

        if (!Instance) continue;
        
        Instance->QuestContentGuid = ContentNode->QuestGuid;
        const FName TagName = MakeNodeTagName(TagPrefix, Label);
        AllCompiledQuestTags.Add(TagName);

        // Register outcome fact tags so prerequisite expressions can reference them as valid gameplay tags.
        if (const UQuestlineNode_Step* QuestStepNode = Cast<UQuestlineNode_Step>(ContentNode))
        {
            if (QuestStepNode->ObjectiveClass)
            {
                const UQuestObjective* ObjCDO = QuestStepNode->ObjectiveClass->GetDefaultObject<UQuestObjective>();
                for (const FGameplayTag& OutcomeTag : ObjCDO->GetPossibleOutcomes())
                {
                    AllCompiledQuestTags.AddUnique(QuestStateTagUtils::MakeNodeOutcomeFact(TagName, OutcomeTag));
                }
            }
        }
        
        AllCompiledNodes.Add(TagName, Instance);
        AllCompiledEditorNodes.Add(TagName, ContentNode);
        NodeInstanceMap.Add(ContentNode, Instance);
    }

    // ---- Pass 1b: setter nodes — create UQuestPrereqGroupNode monitors ----

    for (UEdGraphNode* Node : Graph->Nodes)
    {
        UQuestlineNode_PrerequisiteGroupSetter* Setter = Cast<UQuestlineNode_PrerequisiteGroupSetter>(Node);
        if (!Setter) continue;

        if (Setter->GroupName.IsNone())
        {
            AddWarning(FString::Printf(TEXT("[%s] A Prereq Group Setter has no GroupName set and will be skipped."), *TagPrefix));
            continue;
        }

        const FName GroupTagName = FName(*FString::Printf(TEXT("Quest.Prereq.%s.Satisfied"), *SanitizeTagSegment(Setter->GroupName.ToString())));

        UQuestPrereqGroupNode* Monitor = NewObject<UQuestPrereqGroupNode>(RootGraph);
        Monitor->GroupTag = UGameplayTagsManager::Get().RequestGameplayTag(GroupTagName, false);

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

        AllCompiledNodes.Add(GroupTagName, Monitor);
        AllCompiledQuestTags.Add(GroupTagName);
        MonitorTags.Add(GroupTagName);
    }

    // ---- Pass 1c: utility nodes ----
    TArray<UQuestlineNode_UtilityBase*> UtilityEdNodes;
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
        else if (UQuestlineNode_GroupSignalSetter* GroupSetter = Cast<UQuestlineNode_GroupSignalSetter>(UtilEdNode))
        {
            UGroupSignalSetterNode* Inst = NewObject<UGroupSignalSetterNode>(RootGraph);
            Inst->GroupSignalTag = GroupSetter->GroupSignalTag;
            Instance = Inst;
        }
        else if (UQuestlineNode_GroupSignalGetter* GroupGetter = Cast<UQuestlineNode_GroupSignalGetter>(UtilEdNode))
        {
            UGroupSignalGetterNode* Inst = NewObject<UGroupSignalGetterNode>(RootGraph);
            Inst->GroupSignalTag = GroupGetter->GroupSignalTag;
            Instance = Inst;
        }

        if (!Instance) continue;

        const FName UtilKey = FName(*FString::Printf(TEXT("Util_%s"), *Node->NodeGuid.ToString()));
        UtilityEdNodes.Add(UtilEdNode);
        UtilityNodeKeyMap.Add(Node, UtilKey);
        AllCompiledNodes.Add(UtilKey, Instance);
        AllCompiledEditorNodes.Add(UtilKey, Node);
        // Intentionally NOT added to AllCompiledQuestTags — utility nodes are internal
        // routing scaffolding, not externally tracked quest states
    }
    
    if (bHasErrors) return {};
    
    // ---- Pass 2: output pin wiring ----

    for (UQuestlineNode_ContentBase* ContentNode : ContentNodes)
    {
        // LinkedQuestline nodes are erased; their wiring is handled by ResolvePinToTags when the parent graph encounters them
        // while following the predecessor node's output pins
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

            // Deactivated pin: split routing by destination pin category rather than using ResolvePinToTags
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
                if (Pin->Direction == EGPD_Output && CheckExit(Pin)) { bCompletesParent = true; break; }
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
    
    // ---- Pass 2b: utility node Forward output wiring ----

    for (UQuestlineNode_UtilityBase* UtilEdNode : UtilityEdNodes)
    {
        const FName* UtilKey = UtilityNodeKeyMap.Find(UtilEdNode);
        if (!UtilKey) continue;

        UQuestNodeBase* UtilInst = AllCompiledNodes.FindRef(*UtilKey);
        if (!UtilInst) continue;

        UtilInst->NextNodesOnForward.Empty();

        if (UEdGraphPin* ForwardPin = UtilEdNode->FindPin(TEXT("Forward"), EGPD_Output))
        {
            TArray<FName> ForwardTags;
            ResolvePinToTags(ForwardPin, TagPrefix, BoundaryTagsByOutcome, VisitedAssetPaths, ForwardTags);
            for (const FName& Tag : ForwardTags) UtilInst->NextNodesOnForward.Add(Tag);
        }
    }
    // ---- Resolve entry tags from the graph's Entry node ----

    TArray<FName> EntryTags;
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        if (!Cast<UQuestlineNode_Entry>(Node)) continue;

        for (UEdGraphPin* Pin : Node->Pins)
        {
            if (Pin->Direction == EGPD_Output)
            {
                ResolvePinToTags(Pin, TagPrefix, BoundaryTagsByOutcome, VisitedAssetPaths, EntryTags);
            }
        }
        break;
    }
    
    EntryTags.Append(MonitorTags);
    return EntryTags;
}


// -------------------------------------------------------------------------------------------------
// ResolvePinToTags - the node traversal engine
// -------------------------------------------------------------------------------------------------

void FQuestlineGraphCompiler::ResolvePinToTags(UEdGraphPin* FromPin, const FString& TagPrefix, const TMap<FGameplayTag, TArray<FName>>& BoundaryTagsByOutcome,
                                               TArray<FString>& VisitedAssetPaths, TArray<FName>& OutTags)
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
                AddWarning(FString::Printf(TEXT("[%s] An exit node has no OutcomeTag set."), *TagPrefix));
            }
        }

        // LinkedQuestline: resolve its immediate downstream wiring as the linked graph's boundaries, then recurse
        else if (UQuestlineNode_LinkedQuestline* LinkedNode = Cast<UQuestlineNode_LinkedQuestline>(Node))
        {
            if (LinkedNode->LinkedGraph.IsNull())
            {
                AddWarning(FString::Printf(TEXT("[%s] A LinkedQuestline node has no graph assigned and will be skipped."), *TagPrefix));
                continue;
            }

            UQuestlineGraph* LinkedGraph = LinkedNode->LinkedGraph.LoadSynchronous();
            if (!LinkedGraph)
            {
                AddError(FString::Printf(TEXT("[%s] Failed to load linked graph asset '%s'."), *TagPrefix, *LinkedNode->LinkedGraph.ToString()));
                continue;
            }

            const FString LinkedPath = LinkedGraph->GetPathName();
            if (VisitedAssetPaths.Contains(LinkedPath))
            {
                AddError(FString::Printf(TEXT("Cycle detected: '%s' is already in the current compile stack. Check linked questline references for circular dependencies."), *LinkedPath));
                continue;
            }

            // Build boundary map from this linked node's output pins. Named outcome pins are keyed by their FGameplayTag.
            // Any Outcome is stored under FGameplayTag() (invalid tag) as a catch-all.
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
                    if (OutcomeTag.IsValid())
                    {
                        for (const FName& Tag : PinTags) LinkedBoundaryByOutcome.FindOrAdd(OutcomeTag).AddUnique(Tag);
                    }
                }
                else if (OutputPin->PinName == TEXT("Any Outcome"))
                {
                    // Stored under invalid tag — injected for any exit outcome (see exit node branch)
                    for (const FName& Tag : PinTags) LinkedBoundaryByOutcome.FindOrAdd(FGameplayTag()).AddUnique(Tag);
                }
            }

            VisitedAssetPaths.Add(LinkedPath);

            const FString LinkedPrefix = TagPrefix + TEXT(".") + SanitizeTagSegment(LinkedGraph->QuestlineID.IsEmpty() ? LinkedGraph->GetName() : LinkedGraph->QuestlineID);

            TArray<FName> LinkedEntryTags = CompileGraph(LinkedGraph->QuestlineEdGraph, LinkedPrefix, LinkedBoundaryByOutcome, VisitedAssetPaths);

            VisitedAssetPaths.RemoveSingleSwap(LinkedPath);

            for (const FName& Tag : LinkedEntryTags)
            {
                OutTags.AddUnique(Tag);
            }
        }

        // Quest or Step: return the tag assigned during Pass 1
        else if (UQuestlineNode_ContentBase* ContentNode = Cast<UQuestlineNode_ContentBase>(Node))
        {
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
    return SimpleQuestEditorUtilities::SanitizeQuestlineTagSegment(InLabel);
}

FName FQuestlineGraphCompiler::MakeNodeTagName(const FString& TagPrefix, const FString& SanitizedLabel) const
{
    return FName(*FString::Printf(TEXT("Quest.%s.%s"), *TagPrefix, *SanitizedLabel));
}

void FQuestlineGraphCompiler::AddError(const FString& Message)
{
    bHasErrors = true;
    UE_LOG(LogTemp, Error, TEXT("QuestlineGraphCompiler: %s"), *Message);
}

void FQuestlineGraphCompiler::AddWarning(const FString& Message)
{
    UE_LOG(LogTemp, Warning, TEXT("QuestlineGraphCompiler: %s"), *Message);
}

void FQuestlineGraphCompiler::RegisterCompiledTags(UQuestlineGraph* InGraph)
{
    ISimpleQuestEditorModule::Get().RegisterCompiledTags(
        InGraph->GetPackage()->GetName(),
        InGraph->CompiledQuestTags);
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
        FPrerequisiteExpressionNode ExprNode;
        ExprNode.Type = EPrerequisiteExpressionType::And;
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
    
    // OR
    if (Cast<UQuestlineNode_PrerequisiteOr>(Node))
    {
        FPrerequisiteExpressionNode ExprNode;
        ExprNode.Type = EPrerequisiteExpressionType::Or;
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
            AddWarning(FString::Printf(TEXT("[%s] A Prereq Group Getter has no GroupTag set and will be skipped."), *TagPrefix));
            return INDEX_NONE;
        }
        FPrerequisiteExpressionNode LeafNode;
        LeafNode.Type    = EPrerequisiteExpressionType::Leaf;
        LeafNode.LeafTag = Getter->GroupTag;
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
                    QuestStateTagUtils::MakeStateFact(NodeTagName, QuestStateTagUtils::Leaf_Completed), false);
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
                LeafNode.LeafTag = UGameplayTagsManager::Get().RequestGameplayTag(QuestStateTagUtils::MakeNodeOutcomeFact(NodeTagName, OutcomeTag), false);
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
            return QuestStateTagUtils::MakeNodeOutcomeFact(NodeTagName, OutcomeTag);
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

