// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Nodes/QuestlineNodeBase.h"
#include "QuestlineNode_GroupSetterBase.generated.h"

UCLASS(Abstract)
class SIMPLEQUESTEDITOR_API UQuestlineNode_GroupSetterBase : public UQuestlineNodeBase
{
	GENERATED_BODY()

public:
	virtual bool CanUserDeleteNode() const override { return true; }
	virtual bool CanDuplicateNode() const override { return true; }
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual FText GetPinDisplayName(const UEdGraphPin* Pin) const override { return FText::GetEmpty(); }
};