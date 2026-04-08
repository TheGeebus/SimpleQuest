// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "QuestlineNode_PrerequisiteBase.h"
#include "QuestlineNode_PrerequisiteAnd.generated.h"

UCLASS()
class SIMPLEQUESTEDITOR_API UQuestlineNode_PrerequisiteAnd : public UQuestlineNode_PrerequisiteBase
{
	GENERATED_BODY()
public:
	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual void GetNodeContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const override;

	void AddConditionPin();

private:
	UPROPERTY()
	int32 ConditionPinCount = 2;
};
