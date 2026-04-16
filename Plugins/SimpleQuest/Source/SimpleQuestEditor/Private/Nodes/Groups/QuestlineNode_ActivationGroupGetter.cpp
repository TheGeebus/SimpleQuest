// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Nodes/Groups/QuestlineNode_ActivationGroupGetter.h"

#include "Utilities/SimpleQuestEditorUtils.h"

void UQuestlineNode_ActivationGroupGetter::AllocateDefaultPins()
{
	// Source node — no input. Activated at graph start, subscribes to WorldState fact.
	CreatePin(EGPD_Output, TEXT("QuestActivation"), TEXT("Forward"));
}

FText UQuestlineNode_ActivationGroupGetter::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return NSLOCTEXT("SimpleQuestEditor", "ActivationGroupGetterTitle", "Activation Group: Get");
}
