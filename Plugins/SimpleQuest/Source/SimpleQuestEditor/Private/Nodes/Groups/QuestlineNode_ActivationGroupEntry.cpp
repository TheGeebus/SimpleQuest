// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Nodes/Groups/QuestlineNode_ActivationGroupEntry.h"

#include "SimpleQuestLog.h"
#include "Types/QuestPinRole.h"
#include "Utilities/SimpleQuestEditorUtils.h"


void UQuestlineNode_ActivationGroupEntry::PostLoad()
{
	Super::PostLoad();

	// Migration: Wave 3.b renamed the input pin from "Activate" to "Enter" to distinguish utility-node
	// portal inputs from content-node event-cycle activation. Preserve LinkedTo by mutating the pin in place.
	int32 RenamedCount = 0;
	for (UEdGraphPin* Pin : Pins)
	{
		if (Pin && Pin->Direction == EGPD_Input
			&& Pin->PinType.PinCategory == TEXT("QuestActivation")
			&& Pin->PinName == TEXT("Activate"))
		{
			Pin->PinName = TEXT("Enter");
			++RenamedCount;
		}
	}
	if (RenamedCount > 0)
	{
		UE_LOG(LogSimpleQuest, Log,
			TEXT("UQuestlineNode_ActivationGroupEntry::PostLoad: migrated '%s' — %d 'Activate' pin(s) renamed to 'Enter'."),
			*GetName(), RenamedCount);
	}
}

void UQuestlineNode_ActivationGroupEntry::AllocateDefaultPins()
{
	CreatePin(EGPD_Input,  TEXT("QuestActivation"), TEXT("Enter"));
	CreatePin(EGPD_Output, TEXT("QuestActivation"), TEXT("Forward"));
}

FText UQuestlineNode_ActivationGroupEntry::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return NSLOCTEXT("SimpleQuestEditor", "ActivationGroupEntryTitle", "Activation Group: Entry");
}

void UQuestlineNode_ActivationGroupEntry::GetNodeContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const
{
	Super::GetNodeContextMenuActions(Menu, Context);

	FToolMenuSection& Section = Menu->AddSection(
		TEXT("ActivationGroup"),
		NSLOCTEXT("SimpleQuestEditor", "ActivationGroupSection", "Activation Group")
	);

	FSimpleQuestEditorUtilities::AddExamineGroupConnectionsEntry(
		Section,
		const_cast<UQuestlineNode_ActivationGroupEntry*>(this),
		GetGroupTag()
	);
}

void UQuestlineNode_ActivationGroupEntry::GetPinHoverText(const UEdGraphPin& Pin, FString& HoverTextOut) const
{
	switch (GetPinRole(&Pin))
	{
	case EQuestPinRole::ExecIn:
		HoverTextOut = TEXT(
			"Activation signal entering here publishes this Entry's group\n"
			"tag to WorldState, notifying every Activation Group Exit with\n"
			"a matching tag anywhere in the project.\n"
			"\n"
			"The signal also continues locally through the Forward pin.");
		break;

	case EQuestPinRole::ExecForwardOut:
		HoverTextOut = TEXT(
			"Forwards only signals connected to this Entry's input —\n"
			"not activations arriving at other Entries sharing this group tag.\n"
			"\n"
			"Use an Activation Group Exit to utilize the combined group signal\n"
			"of this Entry node with all others sharing the specified tag.");
		break;

	default:
		Super::GetPinHoverText(Pin, HoverTextOut);
		break;
	}
}
