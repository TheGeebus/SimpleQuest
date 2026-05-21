// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Nodes/Prerequisites/QuestlineNode_PrerequisiteOutcome.h"

#include "Types/QuestPinRole.h"
#include "Utilities/SimpleQuestEditorUtils.h"


void UQuestlineNode_PrerequisiteOutcome::AllocateDefaultPins()
{
	// Single output pin, named to match the other PortalExit-derived prereq leaves (Rule Exit, Fact Tag) so
	// wire-up reads the same across all tag-picker prereq leaves.
	CreatePin(EGPD_Output, TEXT("QuestPrerequisite"), TEXT("Exit"));
}

FText UQuestlineNode_PrerequisiteOutcome::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return NSLOCTEXT("SimpleQuestEditor", "PrereqOutcomeTitle", "Prerequisite: Outcome");
}

FLinearColor UQuestlineNode_PrerequisiteOutcome::GetNodeTitleColor() const
{
	return SQ_ED_NODE_PREREQ_GROUP;
}

void UQuestlineNode_PrerequisiteOutcome::GetPinHoverText(const UEdGraphPin& Pin, FString& HoverTextOut) const
{
	if (GetPinRole(&Pin) == EQuestPinRole::PrereqOut)
	{
		HoverTextOut = TEXT(
			"Satisfied when any quest has resolved with the picked outcome tag (or any descendant via\n"
			"gameplay-tag hierarchy). Context-free — no quest scoping. For quest-scoped outcome semantics,\n"
			"compose via Prereq Rules instead.\n"
			"\n"
			"Wire into a content node's Prerequisites input or into another\n"
			"prerequisite expression (AND / OR / NOT).");
		return;
	}
	Super::GetPinHoverText(Pin, HoverTextOut);
}
