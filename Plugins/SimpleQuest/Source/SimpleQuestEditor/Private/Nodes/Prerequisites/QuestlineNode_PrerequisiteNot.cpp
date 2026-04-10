#include "Nodes/Prerequisites/QuestlineNode_PrerequisiteNot.h"


void UQuestlineNode_PrerequisiteNot::AllocateDefaultPins()
{
	CreatePin(EGPD_Input,  TEXT("QuestPrerequisite"), TEXT("Condition_0"));
	CreatePin(EGPD_Output, TEXT("QuestPrerequisite"), TEXT("PrereqOut"));
}

FText UQuestlineNode_PrerequisiteNot::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return FText::FromString(TEXT("NOT"));
}

FText UQuestlineNode_PrerequisiteNot::GetConditionPinLabel(int32 Index) const
{
	return FText::FromString(TEXT("In"));
}
