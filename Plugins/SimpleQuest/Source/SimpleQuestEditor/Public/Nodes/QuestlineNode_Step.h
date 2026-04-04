// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "QuestlineNode_ContentBase.h"
#include "QuestlineNode_Step.generated.h"

class UQuestStep;

UCLASS()
class SIMPLEQUESTEDITOR_API UQuestlineNode_Step : public UQuestlineNode_ContentBase
{
	GENERATED_BODY()

public:
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;

	/** The step class this node compiles into. */
	UPROPERTY(EditAnywhere, Category = "Quest")
	TSubclassOf<UQuestStep> StepClass;

};
