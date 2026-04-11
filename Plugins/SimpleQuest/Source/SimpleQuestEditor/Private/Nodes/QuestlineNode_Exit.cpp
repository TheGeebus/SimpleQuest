// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Nodes/QuestlineNode_Exit.h"

#include "Utilities/SimpleQuestEditorUtils.h"

#define LOCTEXT_NAMESPACE "SimpleQuestEditor"

void UQuestlineNode_Exit::AllocateDefaultPins()
{
	CreatePin(EGPD_Input, TEXT("QuestActivation"), TEXT("Outcome"));
}

void UQuestlineNode_Exit::AutowireNewNode(UEdGraphPin* FromPin)
{
	if (!FromPin || FromPin->Direction != EGPD_Output) return;
	if (UEdGraphPin* OutcomePin = FindPin(TEXT("Outcome")))
	{
		if (GetSchema()->TryCreateConnection(FromPin, OutcomePin))
		{
			FromPin->GetOwningNode()->NodeConnectionListChanged();
		}
	}
}

FText UQuestlineNode_Exit::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	if (OutcomeTag.IsValid())
	{
		FFormatNamedArguments Args;
		Args.Add("OutcomePrefix", FText::FromName(FName("Outcome")));
		Args.Add("LeafTag", GetTagLeafLabel(OutcomeTag.GetTagName()));
		FText OutText = FText::Format(LOCTEXT("ExitTitleFormat", "{OutcomePrefix}: {LeafTag}"), Args);
		return OutText;
	}
	return NSLOCTEXT("SimpleQuestEditor", "ExitNodeUnset", "Exit (no outcome set)");
}

FLinearColor UQuestlineNode_Exit::GetNodeTitleColor() const
{
	return OutcomeTag.IsValid()
		? FLinearColor(0.9f, 0.7f, 0.1f)   // gold — outcome assigned
		: FLinearColor(0.6f, 0.6f, 0.6f);  // grey — unassigned
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
