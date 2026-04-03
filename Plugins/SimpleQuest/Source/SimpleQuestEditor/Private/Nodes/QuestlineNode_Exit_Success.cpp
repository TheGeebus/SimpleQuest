#include "Nodes/QuestlineNode_Exit_Success.h"

void UQuestlineNode_Exit_Success::AllocateDefaultPins()
{
	CreatePin(EGPD_Input, TEXT("QuestSuccess"), TEXT("Outcome"));
}

FText UQuestlineNode_Exit_Success::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return NSLOCTEXT("SimpleQuestEditor", "ExitSuccess", "Success");
}
