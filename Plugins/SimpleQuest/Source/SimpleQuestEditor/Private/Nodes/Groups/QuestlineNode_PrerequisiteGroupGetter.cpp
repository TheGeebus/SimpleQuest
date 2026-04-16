// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Nodes/Groups/QuestlineNode_PrerequisiteGroupGetter.h"

#include "Utilities/SimpleQuestEditorUtils.h"


void UQuestlineNode_PrerequisiteGroupGetter::AllocateDefaultPins()
{
	CreatePin(EGPD_Output, TEXT("QuestPrerequisite"), TEXT("PrereqOut"));
}

FText UQuestlineNode_PrerequisiteGroupGetter::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return NSLOCTEXT("SimpleQuestEditor", "PrereqGroupGetterTitle", "Prerequisite Group: Get");
}

FLinearColor UQuestlineNode_PrerequisiteGroupGetter::GetNodeTitleColor() const
{
	return SQ_ED_NODE_PREREQ_GROUP;
}
