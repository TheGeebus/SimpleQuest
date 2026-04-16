// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Nodes/Groups/QuestlineNode_PrerequisiteGroupGetter.h"


void UQuestlineNode_PrerequisiteGroupGetter::AllocateDefaultPins()
{
	CreatePin(EGPD_Output, TEXT("QuestPrerequisite"), TEXT("PrereqOut"));
}

FText UQuestlineNode_PrerequisiteGroupGetter::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return FText::FromString(TEXT("Prereq Group: Get"));
}
