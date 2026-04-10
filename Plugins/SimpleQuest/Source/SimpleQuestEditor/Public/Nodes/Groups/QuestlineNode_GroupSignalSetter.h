// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "QuestlineNode_GroupNodeBase.h"
#include "QuestlineNode_GroupSignalSetter.generated.h"

UCLASS()
class SIMPLEQUESTEDITOR_API UQuestlineNode_GroupSignalSetter : public UQuestlineNode_GroupNodeBase
{
	GENERATED_BODY()

public:
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override
	{
		return NSLOCTEXT("SimpleQuestEditor", "GroupSignalSetterTitle", "Group Signal: Set");
	}
};
