// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Nodes/Groups/QuestlineNode_ActivationGroupSetter.h"

void UQuestlineNode_ActivationGroupSetter::AllocateDefaultPins()
{
	CreatePin(EGPD_Input,  TEXT("QuestActivation"), TEXT("Activate"));
	CreatePin(EGPD_Output, TEXT("QuestActivation"), TEXT("Forward"));
}

FText UQuestlineNode_ActivationGroupSetter::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return NSLOCTEXT("SimpleQuestEditor", "ActivationGroupSetterTitle", "Activation Group: Set");
}