// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Nodes/Utility/QuestlineNode_UtilityBase.h"
#include "QuestlineNode_ClearFact.generated.h"

UCLASS()
class SIMPLEQUESTEDITOR_API UQuestlineNode_ClearFact : public UQuestlineNode_UtilityBase
{
	GENERATED_BODY()

public:
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override
	{
		return NSLOCTEXT("SimpleQuestEditor", "ClearFactTitle", "Clear Facts");
	}

	virtual const FGameplayTagContainer& GetTargetQuestTags() const override { return Facts; }
	virtual void SetTargetQuestTags(const FGameplayTagContainer& NewTags) override { Facts = NewTags; }
	/** No namespace filter — facts are deliberately game-agnostic; designers compose with their own tag tree. */
	virtual FString GetTargetQuestTagsFilterString() const override { return FString(); }

	virtual FText GetAuthoringTagsLabel() const override
	{
		return NSLOCTEXT("SimpleQuestEditor", "ClearFactLabel", "Facts to Clear:");
	}

	/** Facts to clear when this node activates. Each is fully removed from WorldState regardless of ref-count. */
	UPROPERTY(EditAnywhere, Category = "Fact")
	FGameplayTagContainer Facts;

	/**
	 * When true, the FactRemoved event is suppressed even on the present→absent transition. Default false matches
	 * UWorldStateSubsystem::ClearFact's default. Use true for silent cleanup (rare; most designer authoring should
	 * leave this false so subscribers see the state change).
	 */
	UPROPERTY(EditAnywhere, Category = "Fact")
	bool bSuppressBroadcast = false;
};