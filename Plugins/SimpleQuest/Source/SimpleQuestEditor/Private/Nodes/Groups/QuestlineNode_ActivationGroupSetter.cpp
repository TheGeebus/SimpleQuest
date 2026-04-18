// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Nodes/Groups/QuestlineNode_ActivationGroupSetter.h"

#include "Utilities/SimpleQuestEditorUtils.h"
//#include "ToolMenu.h"
//#include "ToolMenus.h"

void UQuestlineNode_ActivationGroupSetter::AllocateDefaultPins()
{
	CreatePin(EGPD_Input,  TEXT("QuestActivation"), TEXT("Activate"));
	CreatePin(EGPD_Output, TEXT("QuestActivation"), TEXT("Forward"));
}

FText UQuestlineNode_ActivationGroupSetter::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return NSLOCTEXT("SimpleQuestEditor", "ActivationGroupSetterTitle", "Activation Group: Set");
}

void UQuestlineNode_ActivationGroupSetter::GetNodeContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const
{
	Super::GetNodeContextMenuActions(Menu, Context);

	FToolMenuSection& Section = Menu->AddSection(
		TEXT("ActivationGroup"),
		NSLOCTEXT("SimpleQuestEditor", "ActivationGroupSection", "Activation Group")
	);

	FSimpleQuestEditorUtilities::AddExamineGroupConnectionsEntry(
		Section,
		const_cast<UQuestlineNode_ActivationGroupSetter*>(this),
		GetGroupTag()
	);
}
