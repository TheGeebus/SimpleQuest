// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Quests/Quest.h"

void UQuest::ResetTransientState()
{
	Super::ResetTransientState();
	PendingEntryActivations.Reset();
	ResolvedByEvents.Reset();
}