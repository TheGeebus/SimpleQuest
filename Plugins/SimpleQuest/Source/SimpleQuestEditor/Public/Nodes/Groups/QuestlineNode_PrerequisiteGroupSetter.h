// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "QuestlineNode_GroupSetterBase.h"
#include "QuestlineNode_PrerequisiteGroupSetter.generated.h"


UCLASS()
class SIMPLEQUESTEDITOR_API UQuestlineNode_PrerequisiteGroupSetter : public UQuestlineNode_GroupSetterBase
{
	GENERATED_BODY()
public:
	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual void GetNodeContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const override;

	virtual FGameplayTag GetGroupTag() const override { return GroupTag; }
	virtual void SetGroupTag(const FGameplayTag& NewTag) override { GroupTag = NewTag; }
	virtual FString GetGroupFilterString() const override { return TEXT("QuestPrereqGroup"); }
	virtual void AddInputPin() override { AddConditionPin(); }
	
	void AddConditionPin();

	/** Identifies this group. Referenced by Getter nodes and prerequisite expressions. */
	UPROPERTY(EditAnywhere, Category="Prerequisite Group", meta=(Categories="QuestPrereqGroup"))
	FGameplayTag GroupTag;

private:
	UPROPERTY()
	int32 ConditionPinCount = 1;

	void RemoveConditionPin(UEdGraphPin* PinToRemove);
};