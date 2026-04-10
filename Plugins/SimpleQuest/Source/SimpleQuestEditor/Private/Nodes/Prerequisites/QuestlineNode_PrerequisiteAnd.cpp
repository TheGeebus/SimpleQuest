#include "Nodes/Prerequisites/QuestlineNode_PrerequisiteAnd.h"
#include "ToolMenu.h"
#include "GraphEditorActions.h"

void UQuestlineNode_PrerequisiteAnd::AllocateDefaultPins()
{
	for (int32 i = 0; i < ConditionPinCount; ++i)
	{
		CreatePin(EGPD_Input, TEXT("QuestPrerequisite"), *FString::Printf(TEXT("Condition_%d"), i));
	}
	CreatePin(EGPD_Output, TEXT("QuestPrerequisite"), TEXT("Out"));
}

FText UQuestlineNode_PrerequisiteAnd::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return FText::FromString(TEXT("AND"));
}

void UQuestlineNode_PrerequisiteAnd::GetNodeContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const
{
	Super::GetNodeContextMenuActions(Menu, Context);

	FToolMenuSection& Section = Menu->AddSection(TEXT("PrereqAndNode"), FText::FromString(TEXT("Prerequisite")));
	Section.AddMenuEntry(
		TEXT("AddConditionPin"),
		FText::FromString(TEXT("Add Condition Pin")),
		FText::FromString(TEXT("Adds another condition input to this AND node")),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([NodePtr = const_cast<UQuestlineNode_PrerequisiteAnd*>(this)]()
		{
			NodePtr->AddConditionPin();
			NodePtr->ReconstructNode();
		}))
	);
}

void UQuestlineNode_PrerequisiteAnd::AddConditionPin()
{
	Modify();
	++ConditionPinCount;
}
