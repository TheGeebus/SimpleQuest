// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "QuestlineNode_GroupNodeBase.h"
#include "QuestlineNode_GroupSignalGetter.generated.h"

UCLASS()
class SIMPLEQUESTEDITOR_API UQuestlineNode_GroupSignalGetter : public UQuestlineNode_GroupNodeBase
{
	GENERATED_BODY()

public:
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override
	{
		return NSLOCTEXT("SimpleQuestEditor", "GroupSignalGetterTitle", "Group Signal: Get");
	}
};
