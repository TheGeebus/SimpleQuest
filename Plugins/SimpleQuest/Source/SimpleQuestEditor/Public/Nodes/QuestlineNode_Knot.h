// Copyright 2026, Greg Bussell, All Rights Reserved.
#pragma once

#include "Nodes/QuestlineNodeBase.h"
#include "QuestlineNode_Knot.generated.h"

UCLASS()
class SIMPLEQUESTEDITOR_API UQuestlineNode_Knot : public UQuestlineNodeBase
{
	GENERATED_BODY()
public:
	virtual bool IsPassThroughNode() const override { return true; }
	virtual void AllocateDefaultPins() override;
	virtual EQuestPinRole GetPinRole(const UEdGraphPin* Pin) const override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override { return FText::GetEmpty(); }
	virtual bool ShouldDrawNodeAsControlPointOnly(int32& OutInputPinIndex, int32& OutOutputPinIndex) const override	{ OutInputPinIndex = 0; OutOutputPinIndex = 1; return true; }
	virtual bool CanUserDeleteNode() const override { return true; }
	virtual bool CanDuplicateNode() const override { return true; }
	virtual void AutowireNewNode(UEdGraphPin* FromPin) override;
	FName GetEffectiveCategory() const;
	virtual void NodeConnectionListChanged() override;
};
