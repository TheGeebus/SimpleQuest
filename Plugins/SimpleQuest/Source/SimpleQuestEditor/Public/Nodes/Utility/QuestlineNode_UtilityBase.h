// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Nodes/QuestlineNodeBase.h"
#include "QuestlineNode_UtilityBase.generated.h"

UCLASS(Abstract)
class SIMPLEQUESTEDITOR_API UQuestlineNode_UtilityBase : public UQuestlineNodeBase
{
	GENERATED_BODY()

public:
	virtual void AllocateDefaultPins() override;
	virtual bool IsUtilityNode() const override { return true; }
	virtual bool CanUserDeleteNode() const override { return true; }
	virtual bool CanDuplicateNode() const override { return true; }
	virtual FLinearColor GetNodeTitleColor() const override;
};
