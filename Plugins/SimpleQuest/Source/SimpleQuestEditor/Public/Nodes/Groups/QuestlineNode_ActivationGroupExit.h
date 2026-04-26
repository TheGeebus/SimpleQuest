// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "QuestlineNode_PortalExitBase.h"
#include "QuestlineNode_ActivationGroupExit.generated.h"

UCLASS()
class SIMPLEQUESTEDITOR_API UQuestlineNode_ActivationGroupExit : public UQuestlineNode_PortalExitBase
{
	GENERATED_BODY()

public:
	virtual void PostLoad() override;
	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual void GetNodeContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const override;
	virtual void GetPinHoverText(const UEdGraphPin& Pin, FString& HoverTextOut) const override;
	
	virtual FGameplayTag GetGroupTag() const override { return GroupTag; }
	virtual void SetGroupTag(const FGameplayTag& NewTag) override { GroupTag = NewTag; }
	virtual FString GetTagFilterString() const override { return TEXT("SimpleQuest.QuestActivationGroup"); }
	
	UPROPERTY(EditAnywhere, Category="Activation Group", meta=(Categories="SimpleQuest.QuestActivationGroup"))
	FGameplayTag GroupTag;

	
};