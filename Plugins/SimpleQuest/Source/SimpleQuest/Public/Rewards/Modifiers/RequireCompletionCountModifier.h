// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Rewards/Modifiers/QuestRewardModifier.h"
#include "RequireCompletionCountModifier.generated.h"

/**
 * Reference modifier: pays only while the quest's completion count falls in a range. "Not until the third run," "the
 * first five times only," "every run after the tutorial" - all of it without a new reward class.
 *
 * IT COUNTS THE COMPLETION IT IS ABOUT, on both paths. The quest's own resolution history is framework-owned and
 * advances by exactly one per completion, so an advertisement can predict the run it describes rather than reporting
 * the previous one - which is what a preview is FOR.
 *
 * GRANT ONCE IS THIS WITH BOTH BOUNDS AT 1, and stays a class of its own because that case wants no configuration and
 * reads instantly in the picker.
 */
UCLASS(meta = (DisplayName = "Require Completion Count"))
class SIMPLEQUEST_API URequireCompletionCountModifier : public UQuestRewardModifier
{
	GENERATED_BODY()

protected:
	/** Earliest completion that pays. 1 is the first run. */
	UPROPERTY(EditAnywhere, meta = (ClampMin = "1"), Category = "Modifier")
	int32 FirstCompletion = 1;

	/** Last completion that pays; 0 means no upper bound. */
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0"), Category = "Modifier")
	int32 LastCompletion = 0;

	virtual bool ModifyGrant_Implementation(FQuestRewardContext& Grant, const FQuestRewardActivationContext& Incoming) override;
	virtual void ModifyPreview_Implementation(FQuestRewardPreview& Preview, const FQuestRewardActivationContext& AsIfActivating) override;

private:
	/** Shared verdict, so the grant and the advertisement cannot answer the same question differently. */
	bool PaysOnCompletion(int32 Completion) const;
};

