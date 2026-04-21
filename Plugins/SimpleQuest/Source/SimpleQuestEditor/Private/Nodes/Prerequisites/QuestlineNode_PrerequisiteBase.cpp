// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Nodes/Prerequisites/QuestlineNode_PrerequisiteBase.h"

#include "Utilities/SimpleQuestEditorUtils.h"

FText UQuestlineNode_PrerequisiteBase::GetConditionPinLabel(int32 Index) const
{
	if (Index >= 0 && Index < 26)
	{
		return FText::FromString(FString::ChrN(1, TEXT('A') + Index));
	}
	return FText::FromString(FString::Printf(TEXT("Condition_%d"), Index));
}

FText UQuestlineNode_PrerequisiteBase::GetPinDisplayName(const UEdGraphPin* Pin) const
{
	if (Pin->PinType.PinCategory == TEXT("QuestPrerequisite"))
	{
		const FString Name = Pin->PinName.ToString();

		if (Name == TEXT("Out") || Name == TEXT("PrereqOut")) return FText::FromString(TEXT("Out"));

		if (Name.StartsWith(TEXT("Condition_")))
		{
			const int32 Index = FCString::Atoi(*Name.Mid(10));
			return GetConditionPinLabel(Index);
		}
	}
	return Super::GetPinDisplayName(Pin);
}

FLinearColor UQuestlineNode_PrerequisiteBase::GetNodeTitleColor() const
{
	return SQ_ED_NODE_PREREQ_GROUP;
}

void UQuestlineNode_PrerequisiteBase::GetNodeContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const
{
	Super::GetNodeContextMenuActions(Menu, Context);

	FToolMenuSection& Section = Menu->AddSection(TEXT("PrereqExaminer"),
		NSLOCTEXT("SimpleQuestEditor", "PrereqExaminerSection", "Prerequisite"));

	FSimpleQuestEditorUtilities::AddExaminePrereqExpressionEntry(Section,
		const_cast<UQuestlineNode_PrerequisiteBase*>(this));
}

