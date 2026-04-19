// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "QuestlineNode_PortalExitBase.h"
#include "QuestlineNode_PrerequisiteRuleExit.generated.h"

UCLASS()
class SIMPLEQUESTEDITOR_API UQuestlineNode_PrerequisiteRuleExit : public UQuestlineNode_PortalExitBase
{
	GENERATED_BODY()
public:
	virtual void PostLoad() override;
	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual void GetPinHoverText(const UEdGraphPin& Pin, FString& HoverTextOut) const override;

	virtual FGameplayTag GetGroupTag() const override { return GroupTag; }
	virtual void SetGroupTag(const FGameplayTag& NewTag) override { GroupTag = NewTag; }
	virtual FString GetTagFilterString() const override { return TEXT("QuestPrereqRule"); }
	
	/** The group tag this getter resolves. Must match a Setter node's GroupTag. */
	UPROPERTY(EditAnywhere, Category="Prerequisite Group", meta=(Categories="QuestPrereqRule"))
	FGameplayTag GroupTag;
};