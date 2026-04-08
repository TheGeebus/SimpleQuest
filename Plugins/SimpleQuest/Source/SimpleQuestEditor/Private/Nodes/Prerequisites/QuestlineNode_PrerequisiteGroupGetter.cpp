#include "Nodes/Prerequisites/QuestlineNode_PrerequisiteGroupGetter.h"


void UQuestlineNode_PrerequisiteGroupGetter::AllocateDefaultPins()
{
	CreatePin(EGPD_Output, TEXT("QuestPrerequisite"), TEXT("PrereqOut"));
}

FText UQuestlineNode_PrerequisiteGroupGetter::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return GroupTag.IsValid()
		? FText::FromString(FString::Printf(TEXT("Get: %s"), *GroupTag.GetTagName().ToString()))
		: FText::FromString(TEXT("Get Prereq Group"));
}
