// Copyright 2026, Greg Bussell, All Rights Reserved.
#include "Nodes/QuestlineNode_Exit_Failure.h"

void UQuestlineNode_Exit_Failure::AllocateDefaultPins()
{
	CreatePin(EGPD_Input, TEXT("QuestFailure"), TEXT("Outcome"));
}

FText UQuestlineNode_Exit_Failure::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return NSLOCTEXT("SimpleQuestEditor", "ExitFailure", "Questline Failure");
}
