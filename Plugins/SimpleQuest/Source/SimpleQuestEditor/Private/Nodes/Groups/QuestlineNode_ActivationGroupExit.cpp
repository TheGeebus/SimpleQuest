// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Nodes/Groups/QuestlineNode_ActivationGroupExit.h"

#include "SimpleQuestLog.h"
#include "Types/QuestPinRole.h"
#include "Utilities/SimpleQuestEditorUtils.h"

void UQuestlineNode_ActivationGroupExit::PostLoad()
{
	Super::PostLoad();

	// Migration: Wave 3.b renamed the output pin from "Forward" to "Exit" to reflect that this pin carries
	// a signal arriving from the portal (no local input is being forwarded). Preserve LinkedTo via in-place mutation.
	int32 RenamedCount = 0;
	for (UEdGraphPin* Pin : Pins)
	{
		if (Pin && Pin->Direction == EGPD_Output
			&& Pin->PinType.PinCategory == TEXT("QuestActivation")
			&& Pin->PinName == TEXT("Forward"))
		{
			Pin->PinName = TEXT("Exit");
			++RenamedCount;
		}
	}
	if (RenamedCount > 0)
	{
		UE_LOG(LogSimpleQuest, Log,
			TEXT("UQuestlineNode_ActivationGroupExit::PostLoad: migrated '%s' — %d 'Forward' pin(s) renamed to 'Exit'."),
			*GetName(), RenamedCount);
	}
}

void UQuestlineNode_ActivationGroupExit::AllocateDefaultPins()
{
	// Source node — no input. Activated at graph start, subscribes to WorldState fact.
	CreatePin(EGPD_Output, TEXT("QuestActivation"), TEXT("Exit"));
}

FText UQuestlineNode_ActivationGroupExit::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return NSLOCTEXT("SimpleQuestEditor", "ActivationGroupExitTitle", "Activation Group: Exit");
}

void UQuestlineNode_ActivationGroupExit::GetNodeContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const
{
	Super::GetNodeContextMenuActions(Menu, Context);

	FToolMenuSection& Section = Menu->AddSection(
		TEXT("ActivationGroup"),
		NSLOCTEXT("SimpleQuestEditor", "ActivationGroupSection", "Activation Group")
	);

	FSimpleQuestEditorUtilities::AddExamineGroupConnectionsEntry(
		Section,
		const_cast<UQuestlineNode_ActivationGroupExit*>(this),
		GetGroupTag()
	);
}

void UQuestlineNode_ActivationGroupExit::GetPinHoverText(const UEdGraphPin& Pin, FString& HoverTextOut) const
{
	if (GetPinRole(&Pin) == EQuestPinRole::ExecForwardOut)
	{
		HoverTextOut = TEXT(
			"Fires when any Activation Group Entry with a matching group\n"
			"tag publishes — from this graph or any other.\n"
			"\n"
			"Activates downstream wires in this graph on arrival.");
		return;
	}
	Super::GetPinHoverText(Pin, HoverTextOut);
}
