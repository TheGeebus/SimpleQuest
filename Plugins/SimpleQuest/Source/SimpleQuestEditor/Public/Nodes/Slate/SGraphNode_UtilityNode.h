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

	/** Returns a SetBlocked-specific "Also Deactivate Targets" checkbox row, or SNullWidget for any other utility
	 *  node type. The checkbox edits UQuestlineNode_SetBlocked::bAlsoDeactivateTargets in place; only that one
	 *  utility subclass surfaces the toggle. */
	TSharedRef<SWidget> CreateAlsoDeactivateToggleWidget();
	void OnAlsoDeactivateChanged(ECheckBoxState NewState);

	UQuestlineNode_UtilityBase* UtilityNode = nullptr;
};