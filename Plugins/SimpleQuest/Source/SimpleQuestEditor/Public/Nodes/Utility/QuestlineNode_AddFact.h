// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Nodes/Utility/QuestlineNode_UtilityBase.h"
#include "Subsystems/WorldStateSubsystem.h"
#include "QuestlineNode_AddFact.generated.h"

UCLASS()
class SIMPLEQUESTEDITOR_API UQuestlineNode_AddFact : public UQuestlineNode_UtilityBase
{
	GENERATED_BODY()

public:
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override
	{
		return NSLOCTEXT("SimpleQuestEditor", "AddFactTitle", "Add Facts");
	}

	virtual const FGameplayTagContainer& GetTargetQuestTags() const override { return Facts; }
	virtual void SetTargetQuestTags(const FGameplayTagContainer& NewTags) override { Facts = NewTags; }
	/** No namespace filter — facts are deliberately game-agnostic; designers compose with their own tag tree. */
	virtual FString GetTargetQuestTagsFilterString() const override { return FString(); }

	virtual FText GetAuthoringTagsLabel() const override
	{
		return NSLOCTEXT("SimpleQuestEditor", "AddFactLabel", "Facts to Add:");
	}

	/** Facts to add when this node activates. Each is asserted into WorldState with BroadcastMode below. */
	UPROPERTY(EditAnywhere, Category = "Fact")
	FGameplayTagContainer Facts;

	/**
	 * Boundary-broadcast mode applied to every fact added by this node. See EFactBroadcastMode for semantics.
	 * BoundaryOnly (default) fires the FactAdded event only on 0→1 transitions; Always fires every call;
	 * Suppress never fires.
	 */
	UPROPERTY(EditAnywhere, Category = "Fact")
	EFactBroadcastMode BroadcastMode = EFactBroadcastMode::BoundaryOnly;
};