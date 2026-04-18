// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Nodes/Groups/QuestlineNode_ActivationGroupGetter.h"

#include "Utilities/SimpleQuestEditorUtils.h"

void UQuestlineNode_ActivationGroupGetter::AllocateDefaultPins()
{
	// Source node — no input. Activated at graph start, subscribes to WorldState fact.
	CreatePin(EGPD_Output, TEXT("QuestActivation"), TEXT("Forward"));
}

FText UQuestlineNode_ActivationGroupGetter::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return NSLOCTEXT("SimpleQuestEditor", "ActivationGroupGetterTitle", "Activation Group: Get");
}

void UQuestlineNode_ActivationGroupGetter::GetNodeContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const
{
	Super::GetNodeContextMenuActions(Menu, Context);

	FToolMenuSection& Section = Menu->AddSection(
		TEXT("ActivationGroup"),
		NSLOCTEXT("SimpleQuestEditor", "ActivationGroupSection", "Activation Group")
	);

	FSimpleQuestEditorUtilities::AddExamineGroupConnectionsEntry(
		Section,
		const_cast<UQuestlineNode_ActivationGroupGetter*>(this),
		GetGroupTag()
	);
}
