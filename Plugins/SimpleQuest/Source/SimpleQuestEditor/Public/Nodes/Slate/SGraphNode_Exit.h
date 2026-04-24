// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SGraphNode.h"

class UQuestlineNode_Exit;
struct FGameplayTag;

/**
 * Slate widget for the Outcome (Exit) terminal node. Adds an inline SGameplayTagCombo filtered to the
 * Quest.Outcome namespace so designers can pick the outcome tag directly on the node without opening the
 * Details panel. Title continues to reflect the picked tag via GetNodeTitle's existing format string.
 */
class SIMPLEQUESTEDITOR_API SGraphNode_Exit : public SGraphNode
{
public:
	SLATE_BEGIN_ARGS(SGraphNode_Exit) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UQuestlineNode_Exit* InNode);

	virtual void UpdateGraphNode() override;
	virtual void CreatePinWidgets() override;
	virtual void AddPin(const TSharedRef<SGraphPin>& PinToAdd) override;

private:
	TSharedRef<SWidget> CreateTagPickerWidget();
	TSharedRef<SWidget> CreatePinContentArea();
	void OnOutcomeTagChanged(const FGameplayTag NewTag);

	UQuestlineNode_Exit* ExitNode = nullptr;
};