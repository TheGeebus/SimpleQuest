// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Rewards/Modifiers/QuestRewardModifier.h"
#include "QuestRewardAmountModifier.generated.h"

/**
 * Base for the common case: a modifier that changes an integer amount. Unpacks FQuestRewardAmount, hands the number to
 * ModifyAmount, and repacks - so a subclass writes one function returning a number instead of reaching into an
 * FInstancedStruct.
 *
 * It also declares the payload contract ONCE, here, so every amount modifier refuses non-amount payloads correctly
 * without each subclass reimplementing the check.
 */
UCLASS(Abstract, Blueprintable, EditInlineNew)
class SIMPLEQUEST_API UQuestRewardAmountModifier : public UQuestRewardModifier
{
	GENERATED_BODY()

public:
	virtual bool HandlesPayload(const UScriptStruct* PayloadType) const override;

protected:
	/** Unpacks, calls ModifyAmount, repacks. Override ModifyAmount rather than this. */
	virtual bool ModifyGrant_Implementation(FQuestRewardContext& Grant, const FQuestRewardActivationContext& Incoming) override;

	/** The same, for the advertised form - a fixed amount, or both ends of a range. Override ModifyAmount, not this. */
	virtual bool ModifyPreview_Implementation(FQuestRewardPreview& Preview, const FQuestRewardActivationContext& AsIfActivating) override;

	/**
	 * The whole authoring surface for the common case: given the amount, return the new one. Return zero or less to
	 * drop the grant - a reward worth nothing is worth not publishing. Incoming is here because the reference case
	 * needs it: scaling reads the recipient's own state off the activation Instigator.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, meta = (BlueprintProtected = "true"), Category = "Reward")
	int32 ModifyAmount(int32 Amount, const FQuestRewardActivationContext& Incoming);
};

