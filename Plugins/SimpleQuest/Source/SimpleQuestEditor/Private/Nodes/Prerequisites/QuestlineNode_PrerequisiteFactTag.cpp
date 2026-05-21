// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Nodes/Prerequisites/QuestlineNode_PrerequisiteFactTag.h"

#include "Types/QuestPinRole.h"
#include "Utilities/SimpleQuestEditorUtils.h"


void UQuestlineNode_PrerequisiteFactTag::AllocateDefaultPins()
{
	// Single output pin, named to match Prereq Rule Exit's "Exit" convention so wire-up reads the same
	// across getter-shaped prereq leaves. Pin category QuestPrerequisite gates downstream wiring to
	// Prerequisites inputs and combinator condition inputs.
	CreatePin(EGPD_Output, TEXT("QuestPrerequisite"), TEXT("Exit"));
}

FText UQuestlineNode_PrerequisiteFactTag::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return NSLOCTEXT("SimpleQuestEditor", "PrereqFactTagTitle", "Prerequisite: Fact");
}

FLinearColor UQuestlineNode_PrerequisiteFactTag::GetNodeTitleColor() const
{
	return SQ_ED_NODE_PREREQ_GROUP;
}

void UQuestlineNode_PrerequisiteFactTag::GetPinHoverText(const UEdGraphPin& Pin, FString& HoverTextOut) const
{
	if (GetPinRole(&Pin) == EQuestPinRole::PrereqOut)
	{
		HoverTextOut = TEXT(
			"Satisfied when the picked World State fact tag is present at runtime (HasFact returns true).\n"
			"Decoupled from graph topology — any registered gameplay tag is valid.\n"
			"\n"
			"Wire into a content node's Prerequisites input or into another\n"
			"prerequisite expression (AND / OR / NOT).");
		return;
	}
	Super::GetPinHoverText(Pin, HoverTextOut);
}
