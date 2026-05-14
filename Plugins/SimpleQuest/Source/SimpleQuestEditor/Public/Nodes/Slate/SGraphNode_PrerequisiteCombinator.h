// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "SGraphNode.h"

class UQuestlineNode_PrerequisiteBase;

class SGraphNode_PrerequisiteCombinator : public SGraphNode
{
public:
	SLATE_BEGIN_ARGS(SGraphNode_PrerequisiteCombinator) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UQuestlineNode_PrerequisiteBase* InNode);

	virtual void UpdateGraphNode() override;
	virtual FReply OnAddPin() override;
	virtual void AddPin(const TSharedRef<SGraphPin>& PinToAdd) override;

private:
	UQuestlineNode_PrerequisiteBase* CombinatorNode = nullptr;
	bool bCanAddPin = false;
};