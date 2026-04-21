// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "KismetNodes/SGraphNodeK2Default.h"

struct FGameplayTag;
class UK2Node_CompleteObjectiveWithOutcome;

class SGraphNode_CompleteObjective : public SGraphNodeK2Default
{
public:
	SLATE_BEGIN_ARGS(SGraphNode_CompleteObjective) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UK2Node_CompleteObjectiveWithOutcome* InNode);

protected:
	virtual void CreatePinWidgets() override;

private:
	void OnOutcomeTagChanged(const FGameplayTag NewTag);
};
