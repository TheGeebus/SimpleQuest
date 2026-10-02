// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Nodes/Utility/QuestlineNode_UtilityBase.h"
#include "QuestlineNode_DeactivateQuest.generated.h"

UCLASS()
class SIMPLEQUESTEDITOR_API UQuestlineNode_DeactivateQuest : public UQuestlineNode_UtilityBase
{
	GENERATED_BODY()

public:
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override
	{
		return NSLOCTEXT("SimpleQuestEditor", "DeactivateQuestTitle", "Deactivate Quest");
	}

	virtual FText GetAuthoringTagsLabel() const override
	{
		return NSLOCTEXT("SimpleQuestEditor", "DeactivateQuestLabel", "Tags to Deactivate:");
	}

	virtual const FGameplayTagContainer& GetTargetQuestTags() const override { return TargetQuestTags; }
	virtual void SetTargetQuestTags(const FGameplayTagContainer& NewTags) override { TargetQuestTags = NewTags; }
	virtual FString GetTargetQuestTagsFilterString() const override { return TEXT("SimpleQuest.Questline"); }

	/**
	 * Steps, containers, or whole questline identity tags. A questline's tag tears the questline down through its own
	 * Entry node's Deactivated routing, so the graph being closed decides what closing it means.
	 */
	UPROPERTY(EditAnywhere, Category="Deactivate", meta=(Categories="SimpleQuest.Questline"))
	FGameplayTagContainer TargetQuestTags;
};

