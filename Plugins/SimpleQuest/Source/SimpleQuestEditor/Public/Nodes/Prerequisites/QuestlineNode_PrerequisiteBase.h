// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Nodes/QuestlineNodeBase.h"
#include "QuestlineNode_PrerequisiteBase.generated.h"

UCLASS(Abstract)
class SIMPLEQUESTEDITOR_API UQuestlineNode_PrerequisiteBase : public UQuestlineNodeBase
{
	GENERATED_BODY()
public:
	virtual bool CanUserDeleteNode() const override { return true; }
	virtual bool CanDuplicateNode() const override  { return true; }
	virtual FText GetConditionPinLabel(int32 Index) const;
	virtual FText GetPinDisplayName(const UEdGraphPin* Pin) const override;
	virtual FLinearColor GetNodeTitleColor() const override;

};
