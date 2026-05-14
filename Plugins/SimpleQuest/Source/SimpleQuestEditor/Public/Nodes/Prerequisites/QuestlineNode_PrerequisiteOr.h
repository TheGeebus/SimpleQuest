// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "QuestlineNode_PrerequisiteBase.h"
#include "QuestlineNode_PrerequisiteOr.generated.h"

UCLASS()
class SIMPLEQUESTEDITOR_API UQuestlineNode_PrerequisiteOr : public UQuestlineNode_PrerequisiteBase
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

	void RemoveConditionPin(UEdGraphPin* PinToRemove);
};
