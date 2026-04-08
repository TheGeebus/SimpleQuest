#include "Nodes/Prerequisites/QuestlineNode_PrerequisiteOr.h"
#include "ToolMenu.h"
#include "GraphEditorActions.h"

void UQuestlineNode_PrerequisiteOr::AllocateDefaultPins()
{
	for (int32 i = 0; i < ConditionPinCount; ++i)
	{
		CreatePin(EGPD_Input, TEXT("QuestPrerequisite"), *FString::Printf(TEXT("Condition_%d"), i));
	}
	CreatePin(EGPD_Output, TEXT("QuestPrerequisite"), TEXT("PrereqOut"));
}

FText UQuestlineNode_PrerequisiteOr::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return FText::FromString(TEXT("OR"));
}

void UQuestlineNode_PrerequisiteOr::GetNodeContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const
{
	Super::GetNodeContextMenuActions(Menu, Context);

	FToolMenuSection& Section = Menu->AddSection(TEXT("PrereqOrNode"), FText::FromString(TEXT("Prerequisite")));
	Section.AddMenuEntry(
		TEXT("AddConditionPin"),
		FText::FromString(TEXT("Add Condition Pin")),
		FText::FromString(TEXT("Adds another condition input to this OR node")),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([NodePtr = const_cast<UQuestlineNode_PrerequisiteOr*>(this)]()
		{
			NodePtr->AddConditionPin();
			NodePtr->ReconstructNode();
		}))
	);
}

void UQuestlineNode_PrerequisiteOr::AddConditionPin()
{
	Modify();
	++ConditionPinCount;
}
