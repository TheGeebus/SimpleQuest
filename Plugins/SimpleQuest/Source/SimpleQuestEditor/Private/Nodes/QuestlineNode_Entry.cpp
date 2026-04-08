// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Nodes/QuestlineNode_Entry.h"

void UQuestlineNode_Entry::AllocateDefaultPins()
{
	// Named outcome paths — fire only when this graph was entered via that specific outcome
	for (const FGameplayTag& Tag : IncomingOutcomeTags)
	{
		if (Tag.IsValid()) CreatePin(EGPD_Output, TEXT("QuestOutcome"), Tag.GetTagName());
	}

	// Unconditional path — always fires when this graph activates
	CreatePin(EGPD_Output, TEXT("QuestActivation"), TEXT("Any Outcome"));
}

void UQuestlineNode_Entry::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UQuestlineNode_Entry, IncomingOutcomeTags))
	{
		ReconstructNode();
	}
}

FText UQuestlineNode_Entry::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return NSLOCTEXT("SimpleQuestEditor", "EntryNodeTitle", "Start");
}
