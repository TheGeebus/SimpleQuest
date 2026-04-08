// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "QuestlineNode_PrerequisiteBase.h"
#include "QuestlineNode_PrerequisiteGroupSetter.generated.h"


UCLASS()
class SIMPLEQUESTEDITOR_API UQuestlineNode_PrerequisiteGroupSetter : public UQuestlineNode_PrerequisiteBase
{
	GENERATED_BODY()
public:
	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual void GetNodeContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const override;

	void AddConditionPin();

	/** Identifies this group. Referenced by Getter nodes and Blueprint library calls. */
	UPROPERTY(EditAnywhere, Category="Prerequisite Group")
	FName GroupName;

private:
	UPROPERTY()
	int32 ConditionPinCount = 1;
};
