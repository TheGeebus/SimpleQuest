#include "Nodes/Prerequisites/QuestlineNode_PrerequisiteOr.h"
#include "ToolMenu.h"
#include "GraphEditorActions.h"

void UQuestlineNode_PrerequisiteOr::AllocateDefaultPins()
{
    for (int32 i = 0; i < ConditionPinCount; ++i)
    {
        CreatePin(EGPD_Input, TEXT("QuestPrerequisite"), *FString::Printf(TEXT("Condition_%d"), i));
    }
    CreatePin(EGPD_Output, TEXT("QuestPrerequisite"), TEXT("PrereqOut"));
}

FText UQuestlineNode_PrerequisiteOr::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
    return FText::FromString(TEXT("OR"));
}

void UQuestlineNode_PrerequisiteOr::GetNodeContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const
{
    Super::GetNodeContextMenuActions(Menu, Context);

    FToolMenuSection& Section = Menu->AddSection(TEXT("PrereqOrNode"), FText::FromString(TEXT("Prerequisite")));
    Section.AddMenuEntry(
        TEXT("AddConditionPin"),
        FText::FromString(TEXT("Add Condition Pin")),
        FText::FromString(TEXT("Adds another condition input to this OR node")),
        FSlateIcon(),
        FUIAction(FExecuteAction::CreateLambda([NodePtr = const_cast<UQuestlineNode_PrerequisiteOr*>(this)]()
        {
            NodePtr->AddConditionPin();
        }))
    );

    if (Context->Pin
        && Context->Pin->Direction == EGPD_Input
        && Context->Pin->PinName.ToString().StartsWith(TEXT("Condition_"))
        && ConditionPinCount > 2)
    {
        UEdGraphPin* TargetPin = const_cast<UEdGraphPin*>(Context->Pin);
        Section.AddMenuEntry(
            TEXT("RemoveConditionPin"),
            FText::FromString(TEXT("Remove Condition Pin")),
            FText::FromString(TEXT("Removes this condition input")),
            FSlateIcon(),
            FUIAction(FExecuteAction::CreateLambda([NodePtr = const_cast<UQuestlineNode_PrerequisiteOr*>(this), TargetPin]()
            {
                NodePtr->RemoveConditionPin(TargetPin);
            }))
        );
    }
}

void UQuestlineNode_PrerequisiteOr::AddConditionPin()
{
    ++ConditionPinCount;

    TArray<FName> DesiredNames;
    for (int32 i = 0; i < ConditionPinCount; ++i)
    {
        DesiredNames.Add(*FString::Printf(TEXT("Condition_%d"), i));
    }
    SyncPinsByCategory(EGPD_Input, TEXT("QuestPrerequisite"), DesiredNames);
}

void UQuestlineNode_PrerequisiteOr::RemoveConditionPin(UEdGraphPin* PinToRemove)
{
    if (ConditionPinCount <= 2 || !PinToRemove) return;

    int32 RemoveIndex = -1;
    for (int32 i = 0; i < ConditionPinCount; ++i)
    {
        if (FindPin(*FString::Printf(TEXT("Condition_%d"), i)) == PinToRemove)
        {
            RemoveIndex = i;
            break;
        }
    }
    if (RemoveIndex < 0) return;

    TMap<int32, TArray<UEdGraphPin*>> ShiftedConnections;
    int32 NewIndex = 0;
    for (int32 i = 0; i < ConditionPinCount; ++i)
    {
        if (i == RemoveIndex) continue;
        if (UEdGraphPin* Pin = FindPin(*FString::Printf(TEXT("Condition_%d"), i)))
        {
            if (Pin->LinkedTo.Num() > 0)
            {
                ShiftedConnections.Add(NewIndex, Pin->LinkedTo);
            }
            Pin->BreakAllPinLinks();
        }
        ++NewIndex;
    }
    PinToRemove->BreakAllPinLinks();

    --ConditionPinCount;
    TArray<FName> DesiredNames;
    for (int32 i = 0; i < ConditionPinCount; ++i)
    {
        DesiredNames.Add(*FString::Printf(TEXT("Condition_%d"), i));
    }
    SyncPinsByCategory(EGPD_Input, TEXT("QuestPrerequisite"), DesiredNames);

    for (auto& [Index, LinkedPins] : ShiftedConnections)
    {
        if (UEdGraphPin* Pin = FindPin(*FString::Printf(TEXT("Condition_%d"), Index)))
        {
            for (UEdGraphPin* LinkedPin : LinkedPins)
            {
                Pin->MakeLinkTo(LinkedPin);
            }
        }
    }
}