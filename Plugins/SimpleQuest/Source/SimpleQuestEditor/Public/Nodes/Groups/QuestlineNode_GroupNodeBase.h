// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Nodes/Utility/QuestlineNode_UtilityBase.h"
#include "QuestlineNode_GroupNodeBase.generated.h"

UCLASS(Abstract)
class SIMPLEQUESTEDITOR_API UQuestlineNode_GroupNodeBase : public UQuestlineNode_UtilityBase
{
	GENERATED_BODY()

public:
	virtual FLinearColor GetNodeTitleColor() const override;
	
	UPROPERTY(EditAnywhere, Category="Group Signal", meta=(Categories="Quest"))
	FGameplayTag GroupSignalTag;
};
