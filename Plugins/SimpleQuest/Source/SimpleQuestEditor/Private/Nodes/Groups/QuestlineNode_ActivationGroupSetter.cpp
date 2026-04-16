// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Nodes/Groups/QuestlineNode_ActivationGroupSetter.h"
#include "Utilities/SimpleQuestEditorUtils.h"
#include "ToolMenu.h"

void UQuestlineNode_ActivationGroupSetter::AllocateDefaultPins()
{
	for (int32 i = 0; i < ConditionPinCount; ++i)
	{
		CreatePin(EGPD_Input, TEXT("QuestActivation"), *FString::Printf(TEXT("Activate_%d"), i));
	}
	CreatePin(EGPD_Output, TEXT("QuestActivation"), TEXT("Forward"));
}

FText UQuestlineNode_ActivationGroupSetter::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return NSLOCTEXT("SimpleQuestEditor", "ActivationGroupSetterTitle", "Activation Group: Set");
}

void UQuestlineNode_ActivationGroupSetter::GetNodeContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const
{
	Super::GetNodeContextMenuActions(Menu, Context);

	FToolMenuSection& Section = Menu->AddSection(TEXT("ActivationSetterNode"), FText::FromString(TEXT("Activation Group")));
	Section.AddMenuEntry(
		TEXT("AddConditionPin"),
		FText::FromString(TEXT("Add Input Pin")),
		FText::FromString(TEXT("Adds another activation input to this group setter")),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([NodePtr = const_cast<UQuestlineNode_ActivationGroupSetter*>(this)]()
		{
			NodePtr->AddConditionPin();
		}))
	);

	if (Context->Pin
		&& Context->Pin->Direction == EGPD_Input
		&& Context->Pin->PinName.ToString().StartsWith(TEXT("Activate_"))
		&& ConditionPinCount > 1)
	{
		UEdGraphPin* TargetPin = const_cast<UEdGraphPin*>(Context->Pin);
		Section.AddMenuEntry(
			TEXT("RemoveConditionPin"),
			FText::FromString(TEXT("Remove Input Pin")),
			FText::FromString(TEXT("Removes this activation input")),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([NodePtr = const_cast<UQuestlineNode_ActivationGroupSetter*>(this), TargetPin]()
			{
				NodePtr->RemoveConditionPin(TargetPin);
			}))
		);
	}
}

void UQuestlineNode_ActivationGroupSetter::AddConditionPin()
{
	++ConditionPinCount;

	TArray<FName> DesiredNames;
	for (int32 i = 0; i < ConditionPinCount; ++i)
	{
		DesiredNames.Add(*FString::Printf(TEXT("Activate_%d"), i));
	}
	SyncPinsByCategory(EGPD_Input, TEXT("QuestActivation"), DesiredNames);
}

void UQuestlineNode_ActivationGroupSetter::RemoveConditionPin(UEdGraphPin* PinToRemove)
{
	if (ConditionPinCount <= 1 || !PinToRemove) return;

	int32 RemoveIndex = -1;
	for (int32 i = 0; i < ConditionPinCount; ++i)
	{
		if (FindPin(*FString::Printf(TEXT("Activate_%d"), i)) == PinToRemove)
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
		if (UEdGraphPin* Pin = FindPin(*FString::Printf(TEXT("Activate_%d"), i)))
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
		DesiredNames.Add(*FString::Printf(TEXT("Activate_%d"), i));
	}
	SyncPinsByCategory(EGPD_Input, TEXT("QuestActivation"), DesiredNames);

	for (auto& [Index, LinkedPins] : ShiftedConnections)
	{
		if (UEdGraphPin* Pin = FindPin(*FString::Printf(TEXT("Activate_%d"), Index)))
		{
			for (UEdGraphPin* LinkedPin : LinkedPins)
			{
				Pin->MakeLinkTo(LinkedPin);
			}
		}
	}
}