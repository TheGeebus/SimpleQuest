// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Nodes/Utility/QuestlineNode_UtilityBase.h"
#include "Subsystems/WorldStateSubsystem.h"
#include "QuestlineNode_RemoveFact.generated.h"

UCLASS()
class SIMPLEQUESTEDITOR_API UQuestlineNode_RemoveFact : public UQuestlineNode_UtilityBase
{
	GENERATED_BODY()

public:
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override
	{
		return NSLOCTEXT("SimpleQuestEditor", "RemoveFactTitle", "Remove Facts");
	}

	virtual const FGameplayTagContainer& GetTargetQuestTags() const override { return Facts; }
	virtual void SetTargetQuestTags(const FGameplayTagContainer& NewTags) override { Facts = NewTags; }
	/** No namespace filter — facts are deliberately game-agnostic; designers compose with their own tag tree. */
	virtual FString GetTargetQuestTagsFilterString() const override { return FString(); }

	virtual FText GetAuthoringTagsLabel() const override
	{
		return NSLOCTEXT("SimpleQuestEditor", "RemoveFactLabel", "Facts to Remove:");
	}

	/** Facts to remove when this node activates. Each is asserted into WorldState with BroadcastMode below. */
	UPROPERTY(EditAnywhere, Category = "Fact")
	FGameplayTagContainer Facts;

	/**
	 * Boundary-broadcast mode applied to every fact removed by this node. See EFactBroadcastMode for semantics.
	 * BoundaryOnly (default) fires the FactRemoved event only on 1→0 transitions; Always fires every call;
	 * Suppress never fires.
	 */
	UPROPERTY(EditAnywhere, Category = "Fact")
	EFactBroadcastMode BroadcastMode = EFactBroadcastMode::BoundaryOnly;
};