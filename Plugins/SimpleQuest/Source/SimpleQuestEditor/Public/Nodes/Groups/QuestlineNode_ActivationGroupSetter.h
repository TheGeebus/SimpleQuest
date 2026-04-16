// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "QuestlineNode_GroupSetterBase.h"
#include "QuestlineNode_ActivationGroupSetter.generated.h"

UCLASS()
class SIMPLEQUESTEDITOR_API UQuestlineNode_ActivationGroupSetter : public UQuestlineNode_GroupSetterBase
{
	GENERATED_BODY()

public:
	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	
	UPROPERTY(EditAnywhere, Category="Activation Group", meta=(Categories="QuestActivationGroup"))
	FGameplayTag GroupTag;
};