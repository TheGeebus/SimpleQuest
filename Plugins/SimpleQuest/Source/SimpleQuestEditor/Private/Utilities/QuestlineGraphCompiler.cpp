// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Utilities/QuestlineGraphCompiler.h"
#include "Quests/QuestlineGraph.h"
#include "Quests/Quest.h"
#include "Nodes/QuestlineNode_Quest.h"
#include "Nodes/QuestlineNode_Knot.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "GameplayTagsManager.h"
#include "AssetRegistry/AssetRegistryModule.h"

bool FQuestlineGraphCompiler::Compile(UQuestlineGraph* InGraph)
{
    if (!InGraph || !InGraph->QuestlineEdGraph)
    {
        AddError(TEXT("Invalid graph asset. QuestlineEdGraph is null."));
        return false;
    }

    bHasErrors = false;

    // Derive the effective questline ID. Designer override takes priority, asset name is the fallback
    const FString EffectiveID = SanitizeTagSegment(InGraph->QuestlineID.IsEmpty() ? InGraph->GetName() : InGraph->QuestlineID);

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
        if (TagValue.IsSet() && SanitizeTagSegment(TagValue.GetValue()) == EffectiveID)
        {
            AddError(FString::Printf(
                TEXT("QuestlineID '%s' is already used by '%s'. Set a unique QuestlineID on one of these assets to resolve the conflict."),
                *EffectiveID,
                *Asset.GetObjectPathString()));
            return false;
        }
    }

    CompileInnerGraphs(InGraph);
    if (!bHasErrors)
    {
        CompileOuterGraph(InGraph);
    }

    return !bHasErrors;
}

void FQuestlineGraphCompiler::CompileInnerGraphs(UQuestlineGraph* InGraph)
{
    UE_LOG(LogTemp, Log,
        TEXT("FQuestlineGraphCompiler::CompileInnerGraphs : Skipped... inner step graph not yet implemented."));
}

void FQuestlineGraphCompiler::CompileOuterGraph(UQuestlineGraph* InGraph)
{
    const FString QuestlineName = SanitizeTagSegment(
        InGraph->QuestlineID.IsEmpty() ? InGraph->GetName() : InGraph->QuestlineID);

    TArray<UQuestlineNode_Quest*> QuestNodes;
    
    // Pass 1: validate node label is unique within this graph
    TMap<FString, UQuestlineNode_Quest*> LabelMap;
    for (UEdGraphNode* Node : InGraph->QuestlineEdGraph->Nodes)
    {
        UQuestlineNode_Quest* QuestNode = Cast<UQuestlineNode_Quest>(Node);
        if (!QuestNode) continue;
        QuestNodes.Add(QuestNode);
        
        const FString Label = SanitizeTagSegment(QuestNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString());

        if (Label.IsEmpty())
        {
            AddError(TEXT("A Quest node has an empty label. All Quest nodes must have a label before compiling."));
            continue;
        }

        if (LabelMap.Contains(Label))
        {
            AddError(FString::Printf(
                TEXT("Duplicate Quest node label '%s' found in questline '%s'. Labels must be unique within a graph."),
                *Label, *QuestlineName));
        }
        else
        {
            LabelMap.Add(Label, QuestNode);
        }
    }

    if (bHasErrors) return;

    // Pass 2: clear previously compiled data
    InGraph->Modify();
    InGraph->CompiledQuestTags.Empty();

    // Pass 3: write compiled data to each Quest CDO
    for (UQuestlineNode_Quest* QuestNode : QuestNodes)
    {
        if (!QuestNode->QuestClass)
        {
            AddWarning(FString::Printf(
                TEXT("Quest node '%s' has no QuestClass assigned and will be skipped."),
                *QuestNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString()));
            continue;
        }

        UQuest* CDO = QuestNode->QuestClass->GetDefaultObject<UQuest>();
        if (!CDO)
        {
            AddError(FString::Printf(
                TEXT("Could not retrieve CDO for QuestClass on node '%s'."),
                *QuestNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString()));
            continue;
        }

        CDO->Modify();

        // Write the node's stable GUID to the CDO
        CDO->GetNodeGuid() = QuestNode->QuestGuid;

        // Build, register, and write the Gameplay Tag
        const FString NodeLabel = SanitizeTagSegment(QuestNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
        const FName TagName(*FString::Printf(TEXT("Quest.%s.%s"), *QuestlineName, *NodeLabel));

        UGameplayTagsManager::Get().AddNativeGameplayTag(TagName);
        CDO->QuestTag = UGameplayTagsManager::Get().RequestGameplayTag(TagName);
        InGraph->CompiledQuestTags.Add(TagName);

        // Clear stale relationship data before writing fresh data
        CDO->NextQuestsOnSuccess.Empty();
        CDO->NextQuestsOnFailure.Empty();
        CDO->PrerequisiteQuests.Empty();

        // Resolve Success output: NextQuestsOnSuccess
        if (UEdGraphPin* SuccessPin = QuestNode->FindPin(TEXT("Success"), EGPD_Output))
        {
            TArray<UQuestlineNode_Quest*> SuccessorNodes;
            ResolveConnections(SuccessPin, SuccessorNodes);
            for (const UQuestlineNode_Quest* Successor : SuccessorNodes)
            {
                if (Successor->QuestClass)
                    CDO->NextQuestsOnSuccess.Add(Successor->QuestClass.Get());
            }
        }
        
        // Resolve Failure output: NextQuestsOnFailure
        if (UEdGraphPin* FailurePin = QuestNode->FindPin(TEXT("Failure"), EGPD_Output))
        {
            TArray<UQuestlineNode_Quest*> SuccessorNodes;
            ResolveConnections(FailurePin, SuccessorNodes);
            for (const UQuestlineNode_Quest* Successor : SuccessorNodes)
            {
                if (Successor->QuestClass)
                    CDO->NextQuestsOnFailure.Add(Successor->QuestClass.Get());
            }
        }

        // Resolve Any Outcome output: both sets
        if (UEdGraphPin* AnyOutcomePin = QuestNode->FindPin(TEXT("Any Outcome"), EGPD_Output))
        {
            TArray<UQuestlineNode_Quest*> SuccessorNodes;
            ResolveConnections(AnyOutcomePin, SuccessorNodes);
            for (const UQuestlineNode_Quest* Successor : SuccessorNodes)
            {
                if (Successor->QuestClass)
                {
                    CDO->NextQuestsOnSuccess.Add(Successor->QuestClass.Get());
                    CDO->NextQuestsOnFailure.Add(Successor->QuestClass.Get());
                }
            }
        }

        // Resolve Prerequisites input: source pin category determines required outcome
        if (UEdGraphPin* PrereqPin = QuestNode->FindPin(TEXT("Prerequisites"), EGPD_Input))
        {
            TArray<UEdGraphPin*> SourcePins;
            for (UEdGraphPin* LinkedPin : PrereqPin->LinkedTo)
                SourcePins.Add(LinkedPin);

            for (int32 i = 0; i < SourcePins.Num(); ++i)
            {
                UEdGraphPin* SourcePin = SourcePins[i];
                if (UQuestlineNode_Knot* Knot = Cast<UQuestlineNode_Knot>(SourcePin->GetOwningNode()))
                {
                    if (UEdGraphPin* KnotIn = Knot->FindPin(TEXT("KnotIn"), EGPD_Input))
                    {
                        for (UEdGraphPin* KnotSource : KnotIn->LinkedTo)
                            SourcePins.Add(KnotSource);
                    }
                }
                else if (UQuestlineNode_Quest* SourceNode = Cast<UQuestlineNode_Quest>(SourcePin->GetOwningNode()))
                {
                    if (!SourceNode->QuestClass) continue;

                    FQuestPrerequisite Prereq;
                    Prereq.QuestClass = SourceNode->QuestClass;

                    const FName Category = SourcePin->PinType.PinCategory;
                    if (Category == TEXT("QuestSuccess"))
                        Prereq.RequiredOutcome = EQuestPrerequisiteOutcome::MustSucceed;
                    else if (Category == TEXT("QuestFailure"))
                        Prereq.RequiredOutcome = EQuestPrerequisiteOutcome::MustFail;
                    else
                        Prereq.RequiredOutcome = EQuestPrerequisiteOutcome::AnyOutcome;

                    CDO->PrerequisiteQuests.Add(Prereq);
                }
            }
        }
    }
}

void FQuestlineGraphCompiler::ResolveConnections(UEdGraphPin* FromPin, TArray<UQuestlineNode_Quest*>& OutNodes)
{
    for (UEdGraphPin* LinkedPin : FromPin->LinkedTo)
    {
        UEdGraphNode* LinkedNode = LinkedPin->GetOwningNode();
        if (UQuestlineNode_Knot* Knot = Cast<UQuestlineNode_Knot>(LinkedNode))
        {
            if (UEdGraphPin* KnotOut = Knot->FindPin(TEXT("KnotOut"), EGPD_Output))
                ResolveConnections(KnotOut, OutNodes);
        }
        else if (UQuestlineNode_Quest* QuestNode = Cast<UQuestlineNode_Quest>(LinkedNode))
        {
            OutNodes.AddUnique(QuestNode);
        }
    }
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
