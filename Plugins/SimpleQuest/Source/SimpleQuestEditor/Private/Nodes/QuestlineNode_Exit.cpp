// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Nodes/QuestlineNode_Exit.h"

#include "Utilities/SimpleQuestEditorUtils.h"

#define LOCTEXT_NAMESPACE "SimpleQuestEditor"

void UQuestlineNode_Exit::AllocateDefaultPins()
{
	CreatePin(EGPD_Input, TEXT("QuestActivation"), TEXT("Outcome"));
}

FText UQuestlineNode_Exit::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	if (OutcomeTag.IsValid())
	{
		FFormatNamedArguments Args;
		Args.Add("OutcomePrefix", FText::FromName(FName("Outcome")));
		Args.Add("LeafTag", GetOutcomeLabel(OutcomeTag.GetTagName()));
		FText OutText = FText::Format(LOCTEXT("ExitTitleFormat", "{OutcomePrefix} - {LeafTag}"), Args);
		return OutText;
	}
	return NSLOCTEXT("SimpleQuestEditor", "ExitNodeUnset", "Outcome (not set)");
}

FLinearColor UQuestlineNode_Exit::GetNodeTitleColor() const
{
	return OutcomeTag.IsValid()
		? SQ_ED_NODE_EXIT_ACTIVE  
		: SQ_ED_NODE_EXIT_INACTIVE; 
}

void UQuestlineNode_Exit::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UQuestlineNode_Exit, OutcomeTag))
	{
		if (UEdGraph* Graph = GetGraph())
		{
			Graph->NotifyGraphChanged();
		}
	}
}

#undef LOCTEXT_NAMESPACE
