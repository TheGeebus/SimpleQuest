// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "QuestlineNode_GroupGetterBase.h"
#include "QuestlineNode_PrerequisiteGroupGetter.generated.h"

UCLASS()
class SIMPLEQUESTEDITOR_API UQuestlineNode_PrerequisiteGroupGetter : public UQuestlineNode_GroupGetterBase
{
	GENERATED_BODY()
public:
	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;

	/** The group tag this getter resolves. Must match a Setter node's GroupTag. */
	UPROPERTY(EditAnywhere, Category="Prerequisite Group", meta=(Categories="QuestPrereqGroup"))
	FGameplayTag GroupTag;
};