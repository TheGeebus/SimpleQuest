#include "Nodes/Prerequisites/QuestlineNode_PrerequisiteGroupSetter.h"
#include "ToolMenu.h"

void UQuestlineNode_PrerequisiteGroupSetter::AllocateDefaultPins()
{
	// Setter is a pure sink — no output pin, only condition inputs
	for (int32 i = 0; i < ConditionPinCount; ++i)
	{
		CreatePin(EGPD_Input, TEXT("QuestPrerequisite"),
			*FString::Printf(TEXT("Condition_%d"), i));
	}
}

FText UQuestlineNode_PrerequisiteGroupSetter::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return GroupName.IsNone()
		? FText::FromString(TEXT("Prereq Group"))
		: FText::FromString(FString::Printf(TEXT("Prereq Group: %s"), *GroupName.ToString()));
}

void UQuestlineNode_PrerequisiteGroupSetter::GetNodeContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const
{
	Super::GetNodeContextMenuActions(Menu, Context);

	FToolMenuSection& Section = Menu->AddSection(TEXT("PrereqSetterNode"), FText::FromString(TEXT("Prerequisite Group")));
	Section.AddMenuEntry(
		TEXT("AddConditionPin"),
		FText::FromString(TEXT("Add Condition Pin")),
		FText::FromString(TEXT("Adds another condition to this prerequisite group")),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([NodePtr = const_cast<UQuestlineNode_PrerequisiteGroupSetter*>(this)]()
		{
			NodePtr->AddConditionPin();
			NodePtr->ReconstructNode();
		}))
	);
}

void UQuestlineNode_PrerequisiteGroupSetter::AddConditionPin()
{
	Modify();
	++ConditionPinCount;
}
