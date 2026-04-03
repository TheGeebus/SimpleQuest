// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Utilities/QuestlineGraphCompiler.h"
#include "Quests/QuestlineGraph.h"
#include "Quests/QuestNodeBase.h"
#include "Quests/QuestStep.h"
#include "Quests/Quest.h"
#include "Nodes/QuestlineNode_ContentBase.h"
#include "Nodes/QuestlineNode_Quest.h"
#include "Nodes/QuestlineNode_Leaf.h"
#include "Nodes/QuestlineNode_LinkedQuestline.h"
#include "Nodes/QuestlineNode_Knot.h"
#include "Nodes/QuestlineNode_Entry.h"
#include "Nodes/QuestlineNode_Exit_Success.h"
#include "Nodes/QuestlineNode_Exit_Failure.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "GameplayTagsManager.h"
#include "AssetRegistry/AssetRegistryModule.h"


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

    // Mark the graph asset as dirty, meaning it needs to be saved
    InGraph->Modify();
    InGraph->CompiledQuestTags.Empty();

    // The graphs that have already been compiled. Provided to CompileGraph, which forwards it to all recursive calls.
    TArray<FString> VisitedAssetPaths;
    VisitedAssetPaths.Add(InGraph->GetPathName());

    // Start recursive compilation, working forward from the Start node. This is the top level so there are no boundary tags to
    // pass in from a parent graph yet.
    CompileGraph(InGraph, TagPrefix, {}, {}, VisitedAssetPaths);

    return !bHasErrors;
}


// -------------------------------------------------------------------------------------------------
// CompileGraph — recursive
// -------------------------------------------------------------------------------------------------

TArray<FGameplayTag> FQuestlineGraphCompiler::CompileGraph(UQuestlineGraph* Graph, const FString& TagPrefix, const TArray<FGameplayTag>& SuccessBoundaryTags,
    const TArray<FGameplayTag>& FailureBoundaryTags, TArray<FString>& VisitedAssetPaths)
{
    if (!Graph || !Graph->QuestlineEdGraph) return {};

    // ---- Pass 1: label uniqueness, GUID write, tag assignment ----

    TArray<UQuestlineNode_ContentBase*> ContentNodes;
    TMap<FString, UQuestlineNode_ContentBase*> LabelMap;

    for (UEdGraphNode* Node : Graph->QuestlineEdGraph->Nodes)
    {
        UQuestlineNode_ContentBase* ContentNode = Cast<UQuestlineNode_ContentBase>(Node);
        if (!ContentNode) continue;
        ContentNodes.Add(ContentNode);

        // LinkedQuestline nodes are compiler-only scaffolding with no CDO; skip tag assignment
        if (Cast<UQuestlineNode_LinkedQuestline>(ContentNode)) continue;

        UQuestNodeBase* CDO = GetCDOForContentNode(ContentNode);
        if (!CDO)
        {
            AddWarning(FString::Printf(TEXT("[%s] Content node '%s' has no class assigned and will be skipped."), *TagPrefix, *ContentNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString()));
            continue;
        }

        const FString Label = SanitizeTagSegment(ContentNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
        if (Label.IsEmpty())
        {
            AddError(FString::Printf(TEXT("[%s] A content node has an empty label. All Quest and Leaf nodes must have a label before compiling."), *TagPrefix));
            continue;
        }

        if (LabelMap.Contains(Label))
        {
            AddError(FString::Printf(TEXT("[%s] Duplicate node label '%s'. Labels must be unique within a graph."), *TagPrefix, *Label));
        }
        else
        {
            LabelMap.Add(Label, ContentNode);
        }

        CDO->Modify();
        CDO->QuestContentGuid = ContentNode->QuestGuid;

        const FName TagName(*FString::Printf(TEXT("Quest.%s.%s"), *TagPrefix, *Label));
        UGameplayTagsManager::Get().AddNativeGameplayTag(TagName);
        CDO->QuestTag = UGameplayTagsManager::Get().RequestGameplayTag(TagName);
        Graph->CompiledQuestTags.Add(TagName);
    }

    if (bHasErrors) return {};

    // ---- Pass 2: output pin wiring ----

    for (UQuestlineNode_ContentBase* ContentNode : ContentNodes)
    {
        // LinkedQuestline nodes are erased; their wiring is handled by ResolvePinToTags when the parent graph encounters them
        // while following the predecessor node's output pins
        if (Cast<UQuestlineNode_LinkedQuestline>(ContentNode)) continue;

        UQuestNodeBase* CDO = GetCDOForContentNode(ContentNode);
        if (!CDO) continue;

        CDO->NextNodesOnSuccess.Empty();
        CDO->NextNodesOnFailure.Empty();

        TArray<FGameplayTag> SuccessTags, FailureTags, AnyOutcomeTags;

        if (UEdGraphPin* SuccessPin = ContentNode->FindPin(TEXT("Success"), EGPD_Output))
        {
            ResolvePinToTags(SuccessPin, TagPrefix, SuccessBoundaryTags, FailureBoundaryTags, VisitedAssetPaths, SuccessTags);
        }
        if (UEdGraphPin* FailurePin = ContentNode->FindPin(TEXT("Failure"), EGPD_Output))
        {
            ResolvePinToTags(FailurePin, TagPrefix, SuccessBoundaryTags, FailureBoundaryTags, VisitedAssetPaths, FailureTags);
        }
        if (UEdGraphPin* AnyOutcomePin = ContentNode->FindPin(TEXT("Any Outcome"), EGPD_Output))
        {
            ResolvePinToTags(AnyOutcomePin, TagPrefix, SuccessBoundaryTags, FailureBoundaryTags, VisitedAssetPaths, AnyOutcomeTags);
        }
        for (const FGameplayTag& Tag : SuccessTags) CDO->NextNodesOnSuccess.Add(Tag);
        for (const FGameplayTag& Tag : FailureTags) CDO->NextNodesOnFailure.Add(Tag);
        for (const FGameplayTag& Tag : AnyOutcomeTags)
        {
            CDO->NextNodesOnSuccess.Add(Tag);
            CDO->NextNodesOnFailure.Add(Tag);
        }
        // A node whose output chain reaches an exit should complete its parent graph on resolution
        UEdGraphPin* SuccessPin = ContentNode->FindPin(TEXT("Success"), EGPD_Output);
        UEdGraphPin* FailurePin = ContentNode->FindPin(TEXT("Failure"), EGPD_Output);
        UEdGraphPin* AnyPin = ContentNode->FindPin(TEXT("Any Outcome"), EGPD_Output);

        CDO->bCompletesParentGraph =
            (SuccessPin && PinLeadsToExit(SuccessPin)) ||
            (FailurePin && PinLeadsToExit(FailurePin)) ||
            (AnyPin && PinLeadsToExit(AnyPin));
    }

    // ---- Resolve entry tags from the graph's Entry node ----

    TArray<FGameplayTag> EntryTags;
    for (UEdGraphNode* Node : Graph->QuestlineEdGraph->Nodes)
    {
        if (!Cast<UQuestlineNode_Entry>(Node)) continue;

        for (UEdGraphPin* Pin : Node->Pins)
        {
            if (Pin->Direction == EGPD_Output)
            {
                ResolvePinToTags(Pin, TagPrefix, SuccessBoundaryTags, FailureBoundaryTags, VisitedAssetPaths, EntryTags);
            }
        }
        break;
    }

    return EntryTags;
}


// -------------------------------------------------------------------------------------------------
// ResolvePinToTags - the node traversal engine
// -------------------------------------------------------------------------------------------------

void FQuestlineGraphCompiler::ResolvePinToTags(UEdGraphPin* FromPin, const FString& TagPrefix, const TArray<FGameplayTag>& SuccessBoundaryTags,
    const TArray<FGameplayTag>& FailureBoundaryTags, TArray<FString>& VisitedAssetPaths, TArray<FGameplayTag>& OutTags)
{
    for (UEdGraphPin* LinkedPin : FromPin->LinkedTo)
    {
        UEdGraphNode* Node = LinkedPin->GetOwningNode();

        // Knot: pass through to the other side
        if (UQuestlineNode_Knot* Knot = Cast<UQuestlineNode_Knot>(Node))
        {
            if (UEdGraphPin* KnotOut = Knot->FindPin(TEXT("KnotOut"), EGPD_Output))
            {
                ResolvePinToTags(KnotOut, TagPrefix, SuccessBoundaryTags, FailureBoundaryTags, VisitedAssetPaths, OutTags);
            }
        }

        // Exit nodes: inject boundary tags for the parent graph so a child graph knows what its exits connect to on the parent level 
        else if (Cast<UQuestlineNode_Exit_Success>(Node))
        {
            for (const FGameplayTag& Tag : SuccessBoundaryTags) OutTags.AddUnique(Tag);
        }
        else if (Cast<UQuestlineNode_Exit_Failure>(Node))
        {
            for (const FGameplayTag& Tag : FailureBoundaryTags) OutTags.AddUnique(Tag);
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

            // What the linked graph's exit nodes should activate = what comes after the LinkedQuestline node in the parent graph.
            // Resolve those from the parent context before recursing.
            TArray<FGameplayTag> LinkedSuccessBoundary, LinkedFailureBoundary;

            if (UEdGraphPin* LinkedSuccessPin = LinkedNode->FindPin(TEXT("Success"), EGPD_Output))
            {
                ResolvePinToTags(LinkedSuccessPin, TagPrefix, SuccessBoundaryTags, FailureBoundaryTags, VisitedAssetPaths, LinkedSuccessBoundary);
            }
            if (UEdGraphPin* LinkedFailurePin = LinkedNode->FindPin(TEXT("Failure"), EGPD_Output))
            {
                ResolvePinToTags(LinkedFailurePin, TagPrefix, SuccessBoundaryTags, FailureBoundaryTags, VisitedAssetPaths, LinkedFailureBoundary);
            }
            // AnyOutcome output feeds both boundary sets
            if (UEdGraphPin* LinkedAnyPin = LinkedNode->FindPin(TEXT("Any Outcome"), EGPD_Output))
            {
                TArray<FGameplayTag> AnyTags;
                ResolvePinToTags(LinkedAnyPin, TagPrefix, SuccessBoundaryTags, FailureBoundaryTags, VisitedAssetPaths, AnyTags);
                for (const FGameplayTag& Tag : AnyTags)
                {
                    LinkedSuccessBoundary.AddUnique(Tag);
                    LinkedFailureBoundary.AddUnique(Tag);
                }
            }

            VisitedAssetPaths.Add(LinkedPath);
            LinkedGraph->Modify();
            LinkedGraph->CompiledQuestTags.Empty();

            const FString LinkedPrefix = SanitizeTagSegment(LinkedGraph->QuestlineID.IsEmpty() ? LinkedGraph->GetName() : LinkedGraph->QuestlineID);

            // Recursion on the linked graph asset, providing the context gathered from this node's parent graph, including any context
            // that parent graph may have been provided by a parent graph of its own
            TArray<FGameplayTag> LinkedEntryTags = CompileGraph(LinkedGraph, LinkedPrefix, LinkedSuccessBoundary, LinkedFailureBoundary, VisitedAssetPaths);

            VisitedAssetPaths.RemoveSingleSwap(LinkedPath);

            // The entry tags of the compiled linked graph are what "flows through" the LinkedQuestline node
            for (const FGameplayTag& Tag : LinkedEntryTags)
            {
                OutTags.AddUnique(Tag);
            }
        }

        // Quest or Leaf: return the QuestTag already assigned in CompileGraph Pass 1
        else if (UQuestlineNode_ContentBase* ContentNode = Cast<UQuestlineNode_ContentBase>(Node))
        {
            if (const UQuestNodeBase* CDO = GetCDOForContentNode(ContentNode))
            {
                if (CDO->QuestTag.IsValid())
                    OutTags.AddUnique(CDO->QuestTag);
            }
        }
    }
}


// -------------------------------------------------------------------------------------------------
// Helpers
// -------------------------------------------------------------------------------------------------

UQuestNodeBase* FQuestlineGraphCompiler::GetCDOForContentNode(UQuestlineNode_ContentBase* ContentNode) const
{
    if (const UQuestlineNode_Quest* QuestNode = Cast<UQuestlineNode_Quest>(ContentNode))
    {
        if (!QuestNode->QuestClass) return nullptr;
        return Cast<UQuestNodeBase>(QuestNode->QuestClass->GetDefaultObject());
    }
    if (const UQuestlineNode_Leaf* LeafNode = Cast<UQuestlineNode_Leaf>(ContentNode))
    {
        if (!LeafNode->StepClass) return nullptr;
        return Cast<UQuestNodeBase>(LeafNode->StepClass->GetDefaultObject());
    }
    return nullptr;
}

bool FQuestlineGraphCompiler::PinLeadsToExit(UEdGraphPin* FromPin)
{
    for (UEdGraphPin* LinkedPin : FromPin->LinkedTo)
    {
        UEdGraphNode* Node = LinkedPin->GetOwningNode();
        if (Cast<UQuestlineNode_Exit_Success>(Node) || Cast<UQuestlineNode_Exit_Failure>(Node))
            return true;
        if (const UQuestlineNode_Knot* Knot = Cast<UQuestlineNode_Knot>(Node))
        {
            if (UEdGraphPin* KnotOut = Knot->FindPin(TEXT("KnotOut"), EGPD_Output))
                if (PinLeadsToExit(KnotOut)) return true;
        }
    }
    return false;
}

FString FQuestlineGraphCompiler::SanitizeTagSegment(const FString& InLabel) const
{
    FString Result = InLabel.TrimStartAndEnd();
    for (TCHAR& Char : Result)
    {
        if (!FChar::IsAlnum(Char) && Char != TEXT('_'))
            Char = TEXT('_');
    }
    return Result;
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
