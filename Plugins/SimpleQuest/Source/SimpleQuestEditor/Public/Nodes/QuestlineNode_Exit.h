// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "GameplayTagContainer.h"
#include "QuestlineNodeBase.h"
#include "QuestlineNode_Exit.generated.h"

UCLASS()
class SIMPLEQUESTEDITOR_API UQuestlineNode_Exit : public UQuestlineNodeBase
{
	GENERATED_BODY()
	
public
	:
	virtual bool IsExitNode() const override { return true; }
	virtual bool CanUserDeleteNode() const override { return true; }
	virtual bool CanDuplicateNode() const override { return true; }
	virtual void AllocateDefaultPins() override;
	virtual void AutowireNewNode(UEdGraphPin* FromPin) override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

	/** The outcome this exit represents. Left unset while the graph is being sketched — compiler warns, not errors. */
	UPROPERTY(EditAnywhere, Category = "Exit", meta = (Categories = "Quest.Outcome"))
	FGameplayTag OutcomeTag;
};
