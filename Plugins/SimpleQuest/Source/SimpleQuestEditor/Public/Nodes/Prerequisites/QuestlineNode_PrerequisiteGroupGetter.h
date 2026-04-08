// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "QuestlineNode_PrerequisiteBase.h"
#include "QuestlineNode_PrerequisiteGroupGetter.generated.h"

UCLASS()
class SIMPLEQUESTEDITOR_API UQuestlineNode_PrerequisiteGroupGetter : public UQuestlineNode_PrerequisiteBase
{
	GENERATED_BODY()
public:
	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;

	/** The group tag this getter resolves. Must match a Setter node's GroupName in the compiled tag namespace. */
	UPROPERTY(EditAnywhere, Category="Prerequisite Group")
	FGameplayTag GroupTag;
};
