// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SGraphNode.h"

struct FGameplayTag;
class UQuestlineNode_GroupSetterBase;
class UQuestlineNode_GroupGetterBase;

class SIMPLEQUESTEDITOR_API SGraphNode_GroupNode : public SGraphNode
{
public:
	SLATE_BEGIN_ARGS(SGraphNode_GroupNode) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UEdGraphNode* InNode);

	virtual void UpdateGraphNode() override;
	virtual void CreatePinWidgets() override;
	virtual void AddPin(const TSharedRef<SGraphPin>& PinToAdd) override;

private:
	TSharedRef<SWidget> CreateTagPickerWidget();
	TSharedRef<SWidget> CreatePinContentArea();
	TSharedRef<SWidget> CreateAddPinButton();

	void OnGroupTagChanged(const FGameplayTag NewTag);
	FReply OnAddPinClicked();

	UQuestlineNode_GroupSetterBase* SetterNode = nullptr;
	UQuestlineNode_GroupGetterBase* GetterNode = nullptr;

	bool bIsSetter = false;

	/** Owned by the right side of the pin overlay; holds output pin + add button. */
	TSharedPtr<SVerticalBox> RightColumn;
};