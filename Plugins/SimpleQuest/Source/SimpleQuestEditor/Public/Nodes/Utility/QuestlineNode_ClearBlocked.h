// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Nodes/Utility/QuestlineNode_UtilityBase.h"
#include "QuestlineNode_ClearBlocked.generated.h"

UCLASS()
class SIMPLEQUESTEDITOR_API UQuestlineNode_ClearBlocked : public UQuestlineNode_UtilityBase
{
	GENERATED_BODY()

public:
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override
	{
		return NSLOCTEXT("SimpleQuestEditor", "ClearBlockedTitle", "Clear Blocked");
	}

	UPROPERTY(EditAnywhere, Category="Blocked")
	FGameplayTagContainer TargetQuestTags;
};
