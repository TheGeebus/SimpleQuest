// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Utilities/QuestlineGraphTraversalPolicy.h"
#include "EdGraph/EdGraphPin.h"
#include "Nodes/QuestlineNodeBase.h"
#include "Nodes/QuestlineNode_Exit.h"
#include "Nodes/QuestlineNode_ContentBase.h"



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
