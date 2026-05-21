// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "QuestReward.generated.h"

/**
 * Base class containing data and/or logic that determines quest rewards, like items or attributes such as experience
 * points, in-game currency, health, etc.
 */
UCLASS(Blueprintable)
class SIMPLEQUEST_API UQuestReward : public UObject
{
	GENERATED_BODY()
	
};
