// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SGraphNode.h"

struct FGameplayTagContainer;
class UQuestlineNode_UtilityBase;

class SIMPLEQUESTEDITOR_API SGraphNode_UtilityNode : public SGraphNode
{
public:
	SLATE_BEGIN_ARGS(SGraphNode_UtilityNode) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UEdGraphNode* InNode);

	virtual void UpdateGraphNode() override;
	virtual void CreatePinWidgets() override;
	virtual void AddPin(const TSharedRef<SGraphPin>& PinToAdd) override;

private:
	TSharedRef<SWidget> CreateTagPickerWidget();
	TSharedRef<SWidget> CreatePinContentArea();
	void OnTargetTagsChanged(const FGameplayTagContainer& NewTags);

	UQuestlineNode_UtilityBase* UtilityNode = nullptr;
};