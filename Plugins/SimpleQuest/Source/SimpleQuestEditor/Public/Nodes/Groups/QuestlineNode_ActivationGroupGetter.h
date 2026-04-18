// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "QuestlineNode_GroupGetterBase.h"
#include "QuestlineNode_ActivationGroupGetter.generated.h"

UCLASS()
class SIMPLEQUESTEDITOR_API UQuestlineNode_ActivationGroupGetter : public UQuestlineNode_GroupGetterBase
{
	GENERATED_BODY()

public:
	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual void GetNodeContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const override;

	virtual FGameplayTag GetGroupTag() const override { return GroupTag; }
	virtual void SetGroupTag(const FGameplayTag& NewTag) override { GroupTag = NewTag; }
	virtual FString GetGroupFilterString() const override { return TEXT("QuestActivationGroup"); }
	
	UPROPERTY(EditAnywhere, Category="Activation Group", meta=(Categories="QuestActivationGroup"))
	FGameplayTag GroupTag;

	
};