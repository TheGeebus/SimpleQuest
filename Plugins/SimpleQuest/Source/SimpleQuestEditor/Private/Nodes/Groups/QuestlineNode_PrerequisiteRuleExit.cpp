// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Nodes/Groups/QuestlineNode_PrerequisiteRuleExit.h"

#include "Utilities/SimpleQuestEditorUtils.h"
#include "SimpleQuestLog.h"
#include "Types/QuestPinRole.h"


void UQuestlineNode_PrerequisiteRuleExit::PostLoad()
{
	Super::PostLoad();

	int32 RenamedCount = 0;
	for (UEdGraphPin* Pin : Pins)
	{
		if (Pin && Pin->Direction == EGPD_Output
			&& Pin->PinType.PinCategory == TEXT("QuestPrerequisite")
			&& Pin->PinName == TEXT("PrereqOut"))
		{
			Pin->PinName = TEXT("Exit");
			++RenamedCount;
		}
	}
	if (RenamedCount > 0)
	{
		UE_LOG(LogSimpleQuest, Log,
			TEXT("UQuestlineNode_PrerequisiteRuleExit::PostLoad: '%s' — %d 'PrereqOut' output pin(s) renamed to 'Exit'."),
			*GetName(), RenamedCount);
	}
}

void UQuestlineNode_PrerequisiteRuleExit::AllocateDefaultPins()
{
	CreatePin(EGPD_Output, TEXT("QuestPrerequisite"), TEXT("PrereqOut"));
}

FText UQuestlineNode_PrerequisiteRuleExit::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return NSLOCTEXT("SimpleQuestEditor", "PrereqRuleExitTitle", "Prerequisite Rule: Exit");
}

FLinearColor UQuestlineNode_PrerequisiteRuleExit::GetNodeTitleColor() const
{
	return SQ_ED_NODE_PREREQ_GROUP;
}

void UQuestlineNode_PrerequisiteRuleExit::GetPinHoverText(const UEdGraphPin& Pin, FString& HoverTextOut) const
{
	if (GetPinRole(&Pin) == EQuestPinRole::PrereqOut)
	{
		HoverTextOut = TEXT(
			"Evaluates to the boolean published by the Prerequisite Rule Entry with\n"
			"a matching rule tag — from this graph or any other.\n"
			"\n"
			"Wire into a content node's Prerequisites input or into another\n"
			"prerequisite expression (AND / OR / NOT).");
		return;
	}
	Super::GetPinHoverText(Pin, HoverTextOut);
}
