// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Quests/QuestNodeBase.h"

void UQuestNodeBase::Activate(FGameplayTag InContextualTag)
{
	SetContextualTag(InContextualTag);
	OnNodeActivated.ExecuteIfBound(this, InContextualTag);
}

