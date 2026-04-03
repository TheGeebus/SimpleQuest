// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "QuestNodeBase.h"
#include "QuestNode.generated.h"

/**
 * Concrete internal graph node. Owns a child graph and manages activation of child nodes. UQuest is a temporary subclass
 * of this during the transition to the unified graph model and will be removed at the end of that transition.
 */
UCLASS(Abstract, Blueprintable)
class SIMPLEQUEST_API UQuestNode : public UQuestNodeBase
{
	GENERATED_BODY()
	// Child graph execution model built out with the unified compiler.
};
