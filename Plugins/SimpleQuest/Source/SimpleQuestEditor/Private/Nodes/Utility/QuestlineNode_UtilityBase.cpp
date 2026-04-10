// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Nodes/Utility/QuestlineNode_UtilityBase.h"
#include "Utilities/SimpleQuestEditorUtils.h"

void UQuestlineNode_UtilityBase::AllocateDefaultPins()
{
	CreatePin(EGPD_Input,  TEXT("QuestActivation"), TEXT("Activate"));
	CreatePin(EGPD_Output, TEXT("QuestActivation"), TEXT("Forward"));
}

FLinearColor UQuestlineNode_UtilityBase::GetNodeTitleColor() const
{
	return SQ_ED_NODE_UTILITY;
}
