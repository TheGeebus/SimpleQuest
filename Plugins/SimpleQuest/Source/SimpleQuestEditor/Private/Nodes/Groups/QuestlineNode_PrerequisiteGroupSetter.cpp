// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Nodes/Groups/QuestlineNode_PrerequisiteGroupSetter.h"
#include "ToolMenu.h"
#include "Utilities/SimpleQuestEditorUtils.h"

void UQuestlineNode_PrerequisiteGroupSetter::AllocateDefaultPins()
{
	for (int32 i = 0; i < ConditionPinCount; ++i)
	{
		CreatePin(EGPD_Input, TEXT("QuestPrerequisite"), *FString::Printf(TEXT("Condition_%d"), i));
	}
	CreatePin(EGPD_Output, TEXT("QuestPrerequisite"), TEXT("PrereqOut"));
}

FText UQuestlineNode_PrerequisiteGroupSetter::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return NSLOCTEXT("SimpleQuestEditor", "PrereqGroupSetterTitle", "Prerequisite Group: Set");
}

FLinearColor UQuestlineNode_PrerequisiteGroupSetter::GetNodeTitleColor() const
{
	return SQ_ED_NODE_PREREQ_GROUP;
}

void UQuestlineNode_PrerequisiteGroupSetter::GetNodeContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const
{
	Super::GetNodeContextMenuActions(Menu, Context);

	FToolMenuSection& Section = Menu->AddSection(TEXT("PrereqSetterNode"), FText::FromString(TEXT("Prerequisite Group")));
	Section.AddMenuEntry(
		TEXT("AddConditionPin"),
		FText::FromString(TEXT("Add Condition Pin")),
		FText::FromString(TEXT("Adds another condition to this prerequisite group")),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([NodePtr = const_cast<UQuestlineNode_PrerequisiteGroupSetter*>(this)]()
		{
			NodePtr->AddConditionPin();
		}))
	);

	if (Context->Pin
		&& Context->Pin->Direction == EGPD_Input
		&& Context->Pin->PinName.ToString().StartsWith(TEXT("Condition_"))
		&& ConditionPinCount > 1)
	{
		UEdGraphPin* TargetPin = const_cast<UEdGraphPin*>(Context->Pin);
		Section.AddMenuEntry(
			TEXT("RemoveConditionPin"),
			FText::FromString(TEXT("Remove Condition Pin")),
			FText::FromString(TEXT("Removes this condition input")),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([NodePtr = const_cast<UQuestlineNode_PrerequisiteGroupSetter*>(this), TargetPin]()
			{
				NodePtr->RemoveConditionPin(TargetPin);
			}))
		);
	}
}

void UQuestlineNode_PrerequisiteGroupSetter::AddConditionPin()
{
	++ConditionPinCount;

	TArray<FName> DesiredNames;
	for (int32 i = 0; i < ConditionPinCount; ++i)
	{
		DesiredNames.Add(*FString::Printf(TEXT("Condition_%d"), i));
	}
	SyncPinsByCategory(EGPD_Input, TEXT("QuestPrerequisite"), DesiredNames);
}

void UQuestlineNode_PrerequisiteGroupSetter::RemoveConditionPin(UEdGraphPin* PinToRemove)
{
	if (ConditionPinCount <= 1 || !PinToRemove) return;

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