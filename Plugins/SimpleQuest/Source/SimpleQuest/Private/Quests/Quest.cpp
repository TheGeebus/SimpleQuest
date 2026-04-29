// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Quests/Quest.h"

void UQuest::ResetTransientState()
{
	Super::ResetTransientState();
	PendingEntryActivations.Reset();
}