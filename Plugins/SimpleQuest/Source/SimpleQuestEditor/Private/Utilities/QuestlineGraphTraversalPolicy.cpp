// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Utilities/QuestlineGraphTraversalPolicy.h"
#include "EdGraph/EdGraphPin.h"
#include "Nodes/QuestlineNodeBase.h"
#include "Nodes/QuestlineNode_Exit.h"
#include "Nodes/QuestlineNode_ContentBase.h"
#include "Nodes/Groups/QuestlineNode_ActivationGroupEntry.h"
#include "Nodes/Groups/QuestlineNode_ActivationGroupExit.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Nodes/QuestlineNode_Quest.h"
#include "Nodes/QuestlineNode_LinkedQuestline.h"
#include "Quests/QuestlineGraph.h"
#include "Types/QuestPinRole.h"


// -------------------------------------------------------------------------------------------------
// Classification defaults - may be overridden by subclasses to extend the policy
// -------------------------------------------------------------------------------------------------

bool FQuestlineGraphTraversalPolicy::IsExitNode(const UEdGraphNode* Node) const
{
    const UQuestlineNodeBase* N = Cast<const UQuestlineNodeBase>(Node);
    return N && N->IsExitNode();
}

bool FQuestlineGraphTraversalPolicy::IsContentNode(const UEdGraphNode* Node) const
{
    const UQuestlineNodeBase* N = Cast<const UQuestlineNodeBase>(Node);
    return N && N->IsContentNode();
}

bool FQuestlineGraphTraversalPolicy::IsPassThroughNode(const UEdGraphNode* Node) const
{
    const UQuestlineNodeBase* N = Cast<const UQuestlineNodeBase>(Node);
    return N && N->IsPassThroughNode();
}

const UEdGraphPin* FQuestlineGraphTraversalPolicy::GetPassThroughOutputPin(const UEdGraphNode* Node) const
{
    return const_cast<UEdGraphNode*>(Node)->FindPin(TEXT("KnotOut"), EGPD_Output);
}

const UEdGraphPin* FQuestlineGraphTraversalPolicy::GetPassThroughInputPin(const UEdGraphNode* Node) const
{
    return const_cast<UEdGraphNode*>(Node)->FindPin(TEXT("KnotIn"));
}


// -------------------------------------------------------------------------------------------------
// Forward traversal - fixed logic for moving between nodes
// -------------------------------------------------------------------------------------------------

bool FQuestlineGraphTraversalPolicy::HasDownstreamExit(const UEdGraphPin* OutputPin, TSet<const UEdGraphNode*>& Visited) const
{
    if (!OutputPin) return false;
    for (const UEdGraphPin* Connected : OutputPin->LinkedTo)
    {
        if (!Connected) continue;
        const UEdGraphNode* Node = Connected->GetOwningNode();
        if (Visited.Contains(Node)) continue;
        Visited.Add(Node);
        if (IsExitNode(Node)) return true;
        if (IsPassThroughNode(Node))
        {
            if (HasDownstreamExit(GetPassThroughOutputPin(Node), Visited)) return true;
        }
    }
    return false;
}

bool FQuestlineGraphTraversalPolicy::HasDownstreamContent(const UEdGraphPin* OutputPin, TSet<const UEdGraphNode*>& Visited) const
{
    if (!OutputPin) return false;
    for (const UEdGraphPin* Connected : OutputPin->LinkedTo)
    {
        if (!Connected) continue;
        const UEdGraphNode* Node = Connected->GetOwningNode();
        if (Visited.Contains(Node)) continue;
        Visited.Add(Node);
        if (IsContentNode(Node)) return true;
        if (IsPassThroughNode(Node))
        {   
            if (HasDownstreamContent(GetPassThroughOutputPin(Node), Visited)) return true;
        }
    }
    return false;
}

bool FQuestlineGraphTraversalPolicy::LeadsToNode(const UEdGraphPin* OutputPin, const UEdGraphNode* TargetNode, TSet<const UEdGraphNode*>& Visited) const
{
    if (!OutputPin) return false;
    for (const UEdGraphPin* Connected : OutputPin->LinkedTo)
    {
        if (!Connected) continue;
        const UEdGraphNode* Node = Connected->GetOwningNode();
        if (Node == TargetNode) return true;
        if (Visited.Contains(Node)) continue;
        Visited.Add(Node);
        if (IsPassThroughNode(Node))
        {
            if (LeadsToNode(GetPassThroughOutputPin(Node), TargetNode, Visited)) return true;
        }
    }
    return false;
}

void FQuestlineGraphTraversalPolicy::CollectDownstreamTerminalInputs(const UEdGraphPin* KnotOutPin, TArray<const UEdGraphPin*>& OutTerminalPins, TSet<const UEdGraphNode*>& Visited) const
{
    if (!KnotOutPin) return;
    for (const UEdGraphPin* Connected : KnotOutPin->LinkedTo)
    {
        if (!Connected) continue;
        const UEdGraphNode* Node = Connected->GetOwningNode();
        if (Visited.Contains(Node)) continue;
        Visited.Add(Node);
        if (IsPassThroughNode(Node))
        {
            CollectDownstreamTerminalInputs(GetPassThroughOutputPin(Node), OutTerminalPins, Visited);
        }
        else OutTerminalPins.Add(Connected);
    }
}

void FQuestlineGraphTraversalPolicy::CollectActivationTerminals(const UEdGraphPin* FromOutput, TSet<const UEdGraphPin*>& OutTerminals, TSet<const UEdGraphNode*>& VisitedNodes) const
{
    if (!FromOutput) return;
    for (const UEdGraphPin* Linked : FromOutput->LinkedTo)
    {
        if (!Linked) continue;
        const UEdGraphNode* Node = Linked->GetOwningNode();
        if (!Node) continue;
        if (VisitedNodes.Contains(Node)) continue;
        VisitedNodes.Add(Node);

        // Terminal sink: content or exit node. Only collect activation/deactivation inputs.
        if (IsContentNode(Node) || IsExitNode(Node))
        {
            const FName Cat = Linked->PinType.PinCategory;
            if (Cat == TEXT("QuestActivation") || Cat == TEXT("QuestDeactivate"))
            {
                OutTerminals.Add(Linked);
            }
            continue;
        }

        // Knot: walk KnotOut.
        if (IsPassThroughNode(Node))
        {
            CollectActivationTerminals(GetPassThroughOutputPin(Node), OutTerminals, VisitedNodes);
            continue;
        }

        // Utility node: Activate → Forward. Walk Forward output.
        if (const UQuestlineNodeBase* Base = Cast<const UQuestlineNodeBase>(Node))
        {
            if (Base->IsUtilityNode())
            {
                if (const UEdGraphPin* Forward = Base->GetPinByRole(EQuestPinRole::ExecForwardOut))
                {
                    CollectActivationTerminals(Forward, OutTerminals, VisitedNodes);
                }
                continue;
            }
        }

        // Activation group setter: Activate → Forward. Walk Forward output.
        // (Chained setter → setter is unusual but possible; the walker follows it.)
        if (const UQuestlineNode_ActivationGroupEntry* Setter = Cast<const UQuestlineNode_ActivationGroupEntry>(Node))
        {
            if (const UEdGraphPin* Forward = Setter->GetPinByRole(EQuestPinRole::ExecForwardOut))
            {
                CollectActivationTerminals(Forward, OutTerminals, VisitedNodes);
            }
            continue;
        }
        // Anything else (prereq combinator, getter, etc.) isn't a valid forward destination for activation chains — stop walking this branch.
    }
}


// -------------------------------------------------------------------------------------------------
// Backward traversal - fixed logic
// -------------------------------------------------------------------------------------------------



void FQuestlineGraphTraversalPolicy::CollectKnotInputSources(const UEdGraphPin* KnotInPin, TArray<const UEdGraphPin*>& OutSourcePins, TSet<const UEdGraphNode*>& Visited) const
{
    if (!KnotInPin) return;
    for (const UEdGraphPin* Connected : KnotInPin->LinkedTo)
    {
        if (!Connected) continue;
        const UEdGraphNode* SourceNode = Connected->GetOwningNode();
        if (Visited.Contains(SourceNode)) continue;
        Visited.Add(SourceNode);
        if (IsPassThroughNode(SourceNode))
        {
            if (const UEdGraphPin* UpstreamIn = GetPassThroughInputPin(SourceNode))
            {
                CollectKnotInputSources(UpstreamIn, OutSourcePins, Visited);
            }
        }
        else
        {
            OutSourcePins.Add(Connected);
        }
    }
}

void FQuestlineGraphTraversalPolicy::CollectSourceContentNodes(const UEdGraphPin* Pin, TSet<UQuestlineNode_ContentBase*>& OutSources, TSet<const UEdGraphNode*>& Visited) const
{
    if (!Pin) return;
    const UEdGraphNode* Node = Pin->GetOwningNode();
    if (Visited.Contains(Node)) return;
    Visited.Add(Node);
    if (IsContentNode(Node))
    {
        OutSources.Add(const_cast<UQuestlineNode_ContentBase*>(Cast<const UQuestlineNode_ContentBase>(Node)));
        return;
    }
    if (IsPassThroughNode(Node))
    {
        if (const UEdGraphPin* PassThroughIn = GetPassThroughInputPin(Node))
        {
            for (const UEdGraphPin* Linked : PassThroughIn->LinkedTo)
            {
                CollectSourceContentNodes(Linked, OutSources, Visited);
            }
        }
    }
}

void FQuestlineGraphTraversalPolicy::CollectEffectiveSources(const UEdGraphPin* SourcePin, TSet<const UEdGraphPin*>& OutSources, TSet<const UEdGraphNode*>& VisitedNodes) const
{
    if (!SourcePin) return;
    const UEdGraphNode* OwnerNode = SourcePin->GetOwningNode();
    if (!OwnerNode) return;

    // Direct connection to a content node outcome pin
    if (IsContentNode(OwnerNode))
    {
        OutSources.Add(SourcePin);
        return;
    }

    if (VisitedNodes.Contains(OwnerNode)) return;
    VisitedNodes.Add(OwnerNode);

    // Knot reroute: walk through the matching input side to all connections.
    if (IsPassThroughNode(OwnerNode))
    {
        if (const UEdGraphPin* InPin = GetPassThroughInputPin(OwnerNode))
        {
            for (const UEdGraphPin* Linked : InPin->LinkedTo)
            {
                CollectEffectiveSources(Linked, OutSources, VisitedNodes);
            }
        }
        return;
    }

    // Utility node Forward output: walk through the Activate input to all connections.
    if (const UQuestlineNodeBase* Base = Cast<const UQuestlineNodeBase>(OwnerNode))
    {
        if (Base->IsUtilityNode() && UQuestlineNodeBase::GetPinRoleOf(SourcePin) == EQuestPinRole::ExecForwardOut)
        {
            if (const UEdGraphPin* ActivatePin = Base->GetPinByRole(EQuestPinRole::ExecIn))
            {
                for (const UEdGraphPin* Linked : ActivatePin->LinkedTo)
                {
                    CollectEffectiveSources(Linked, OutSources, VisitedNodes);
                }
            }
            return;
        }
    }

    // Activation group setter Forward output: walk through every Activate_N input.
    if (const UQuestlineNode_ActivationGroupEntry* Setter = Cast<const UQuestlineNode_ActivationGroupEntry>(OwnerNode))
    {
        if (UQuestlineNodeBase::GetPinRoleOf(SourcePin) == EQuestPinRole::ExecForwardOut)
        {
            for (const UEdGraphPin* Pin : Setter->Pins)
            {
                if (Pin->Direction != EGPD_Input) continue;
                if (Pin->PinType.PinCategory != TEXT("QuestActivation")) continue;
                for (const UEdGraphPin* Linked : Pin->LinkedTo)
                {
                    CollectEffectiveSources(Linked, OutSources, VisitedNodes);
                }
            }
        }
        return;
    }

    // Activation group getter Forward output: dereference the group tag to find every setter in the same graph with matching
    // GroupTag, recurse into its input wires.
    if (const UQuestlineNode_ActivationGroupExit* Getter = Cast<const UQuestlineNode_ActivationGroupExit>(OwnerNode))
    {
        if (UQuestlineNodeBase::GetPinRoleOf(SourcePin) == EQuestPinRole::ExecForwardOut)
        {
            const FGameplayTag GetterTag = Getter->GroupTag;
            if (!GetterTag.IsValid()) return;

            if (const UEdGraph* Graph = Getter->GetGraph())
            {
                for (UEdGraphNode* Node : Graph->Nodes)
                {
                    const UQuestlineNode_ActivationGroupEntry* SiblingSetter = Cast<UQuestlineNode_ActivationGroupEntry>(Node);
                    if (!SiblingSetter || SiblingSetter->GroupTag != GetterTag) continue;
                    if (VisitedNodes.Contains(SiblingSetter)) continue;
                    VisitedNodes.Add(SiblingSetter);

                    for (const UEdGraphPin* Pin : SiblingSetter->Pins)
                    {
                        if (Pin->Direction != EGPD_Input) continue;
                        if (Pin->PinType.PinCategory != TEXT("QuestActivation")) continue;
                        for (const UEdGraphPin* Linked : Pin->LinkedTo)
                        {
                            CollectEffectiveSources(Linked, OutSources, VisitedNodes);
                        }
                    }
                }
            }
        }
        return;
    }

    // Anything else (Entry node's "Any Outcome", prereq nodes, etc.) is not a pass-through for activation chains; treat as
    // an end point by adding the pin itself so callers can distinguish Entry's Any Outcome from content outcome if they
    // need to.
    OutSources.Add(SourcePin);
}

const UQuestlineGraph* FQuestlineGraphTraversalPolicy::ResolveContainingAsset(const UEdGraph* Graph)
{
    while (Graph)
    {
        UObject* Outer = Graph->GetOuter();
        if (const UQuestlineGraph* Asset = Cast<UQuestlineGraph>(Outer))
        {
            return Asset;
        }
        if (const UEdGraphNode* ContainerNode = Cast<UEdGraphNode>(Outer))
        {
            Graph = ContainerNode->GetGraph();
            continue;
        }
        return nullptr;
    }
    return nullptr;
}

void FQuestlineGraphTraversalPolicy::CollectEntryReachingSources(const UEdGraph* ChildGraph, TSet<FQuestEffectiveSource>& OutSources) const
{
    if (!ChildGraph) return;
    UObject* GraphOuter = ChildGraph->GetOuter();
    if (!GraphOuter) return;

    // Helper: given an Activate pin on some container node in some parent graph, walk each incoming wire via CollectEffectiveSources
    // and tag each terminal with the asset that owns the source pin.
    auto CollectFromActivatePin = [this, &OutSources](const UEdGraphNode* ContainerNode)
    {
        if (!ContainerNode) return;
        const UEdGraphPin* ActivatePin = UQuestlineNodeBase::FindPinByRole(ContainerNode, EQuestPinRole::ExecIn);
        if (!ActivatePin) return;

        for (const UEdGraphPin* Wire : ActivatePin->LinkedTo)
        {
            TSet<const UEdGraphPin*> PinSources;
            TSet<const UEdGraphNode*> Visited;
            CollectEffectiveSources(Wire, PinSources, Visited);

            for (const UEdGraphPin* Source : PinSources)
            {
                const UEdGraphNode* OwnerNode = Source ? Source->GetOwningNode() : nullptr;
                const UEdGraph* OwnerGraph = OwnerNode ? OwnerNode->GetGraph() : nullptr;
                const UQuestlineGraph* Asset = ResolveContainingAsset(OwnerGraph);

                FQuestEffectiveSource Entry;
                Entry.Pin = Source;
                Entry.Asset = Asset;
                OutSources.Add(Entry);
            }
        }
    };

    // Case 1: ChildGraph is owned by a Quest node — inline inner graph. The parent graph is the Quest node's graph, and
    // the source pins live in that same asset as the child.
    if (const UQuestlineNode_Quest* ParentQuestNode = Cast<UQuestlineNode_Quest>(GraphOuter))
    {
        CollectFromActivatePin(ParentQuestNode);
        return;
    }

    // Case 2: ChildGraph is the top-level graph of a UQuestlineGraph asset. Scan the asset registry for other questline
    // graphs containing LinkedQuestline nodes that reference this asset, and walk each such LinkedQuestline's Activate
    // input in its own graph.
    if (UQuestlineGraph* QuestlineAsset = Cast<UQuestlineGraph>(GraphOuter))
    {
        const IAssetRegistry& AR =
            FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

        TArray<FAssetData> AllGraphAssets;
        AR.GetAssetsByClass(UQuestlineGraph::StaticClass()->GetClassPathName(), AllGraphAssets);

        const FSoftObjectPath OurPath(QuestlineAsset);

        for (const FAssetData& AssetData : AllGraphAssets)
        {
            UQuestlineGraph* OtherAsset = Cast<UQuestlineGraph>(AssetData.GetAsset());
            if (!OtherAsset || OtherAsset == QuestlineAsset || !OtherAsset->QuestlineEdGraph)
                continue;

            for (const UEdGraphNode* Node : OtherAsset->QuestlineEdGraph->Nodes)
            {
                const UQuestlineNode_LinkedQuestline* LinkedNode =
                    Cast<UQuestlineNode_LinkedQuestline>(Node);
                if (!LinkedNode || LinkedNode->LinkedGraph.ToSoftObjectPath() != OurPath) continue;

                CollectFromActivatePin(LinkedNode);
            }
        }
    }
}


// -------------------------------------------------------------------------------------------------
// Composite reachability - fixed logic
// -------------------------------------------------------------------------------------------------

FQuestlineGraphTraversalPolicy::FPinReachability FQuestlineGraphTraversalPolicy::ComputeForwardReachability(const UEdGraphPin* OutputPin) const
{
    FPinReachability Result;
    { TSet<const UEdGraphNode*> V; Result.bReachesExit = HasDownstreamExit(OutputPin, V); }
    { TSet<const UEdGraphNode*> V; Result.bReachesContent = HasDownstreamContent(OutputPin, V); }
    return Result;
}

FQuestlineGraphTraversalPolicy::FPinReachability FQuestlineGraphTraversalPolicy::ComputeFullReachability(const UEdGraphPin* OutputPin, const UEdGraphNode* OutputNode) const
{
    FPinReachability Result = ComputeForwardReachability(OutputPin);
    if (!Result.IsMixed() && IsPassThroughNode(OutputNode))
    {
        if (const UEdGraphPin* PassThroughIn = GetPassThroughInputPin(OutputNode))
        {
            TArray<const UEdGraphPin*> SourcePins;
            TSet<const UEdGraphNode*> Visited;
            CollectKnotInputSources(PassThroughIn, SourcePins, Visited);
            for (const UEdGraphPin* Source : SourcePins)
            {
                const FPinReachability SR = ComputeForwardReachability(Source);
                Result.bReachesExit |= SR.bReachesExit;
                Result.bReachesContent |= SR.bReachesContent;
                if (Result.IsMixed()) break;
            }
        }
    }
    return Result;
}

