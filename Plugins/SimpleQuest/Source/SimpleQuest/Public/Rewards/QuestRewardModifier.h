// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Quests/Types/QuestRewardActivationContext.h"
#include "Quests/Types/QuestRewardContext.h"
#include "Quests/Types/QuestRewardPayloads.h"
#include "Quests/Types/QuestRewardPreview.h"
#include "QuestRewardModifier.generated.h"

struct FQuestRewardPreview;

/**
 * Base class for a reward modifier - the TRANSFORM half of the reward model. A SOURCE (UQuestRewardBase) decides what
 * to grant; a modifier changes a grant on its way out: scale by level, cap at a maximum, split across a party,
 * redirect to a shared stash, drop unless a condition holds.
 *
 * WHY A SEPARATE LAYER RATHER THAN FIELDS ON A REWARD. A transform fused into a source costs a class per combination
 * and only works for the sources somebody thought to fuse it into. UScaledAmountReward is that cost already paid once
 * - and because it fuses "grant an amount" with "scale it," scaled LOOT cannot be authored at all today: a loot table
 * computes its amounts internally, per rolled row, so there is no field to reach. A modifier runs after the grants are
 * queued and catches every one of them uniformly.
 *
 * EditInlineNew + Blueprintable, exactly like a reward: modifiers are an Instanced array on the reward they modify,
 * and a designer subclasses in Blueprint with no C++.
 */
UCLASS(Abstract, Blueprintable, EditInlineNew)
class SIMPLEQUEST_API UQuestRewardModifier : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Caller-facing entry point. Thin C++ forwarder to the protected BlueprintNativeEvent so Blueprint overrides fire -
	 * mirrors UQuestRewardBase::DispatchTryGrantReward. Returns false when the modifier dropped the grant.
	 */
	bool DispatchModifyGrant(FQuestRewardContext& Grant, const FQuestRewardActivationContext& Incoming);

	/** Advertisement twin of DispatchModifyGrant. Returns false when the modifier hid the preview. */
	bool DispatchModifyPreview(FQuestRewardPreview& Preview, AActor* Viewer);

	/**
	 * Whether this modifier operates on a given payload struct; the default accepts anything. Checked BEFORE either
	 * Modify entry point, and a refusal warns naming the modifier and the payload rather than passing silently. Scaling
	 * an amount is meaningful; scaling a "start the cutscene" payload is not, and a modifier that quietly did nothing
	 * would look exactly like one that worked. Same contract as the prerequisite field-role check, for the same reason.
	 *
	 * A PREDICATE rather than one required type, because a payload's grant form and its advertised form differ: an
	 * amount modifier operates on FQuestRewardAmount when granting and on FQuestRewardAmountRange when a loot roll
	 * advertises what it might drop, and both are its business.
	 */
	virtual bool HandlesPayload(const UScriptStruct* PayloadType) const { return true; }

protected:
	/**
	 * Transform one queued grant. Mutate it in place - RewardType, Recipient and CustomData are all yours. Return FALSE
	 * to DROP the grant entirely, which is how drop-unless-condition works.
	 *
	 * *** ONE GRANT IN, ONE OR NONE OUT. *** A modifier cannot emit more: emitting N grants is what a SOURCE does, and
	 * keeping that line sharp is the whole reason this is a separate layer rather than another kind of reward.
	 *
	 * Provenance is NOT yours. Instigator, OriginTag, OriginChain and OriginatingEventID are stamped AFTER modifiers
	 * run, so a modifier cannot corrupt the lineage a recipient uses to tell where a grant came from.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, meta = (BlueprintProtected = "true"), Category = "Reward")
	bool ModifyGrant(UPARAM(ref) FQuestRewardContext& Grant, const FQuestRewardActivationContext& Incoming);

	/**
	 * Transform what the reward ADVERTISES, so "do this, get this" shows the number a player will actually receive.
	 * Return FALSE to hide the preview, which is what a drop-unless-condition modifier should do when its condition
	 * already fails - promising something that will not arrive is worse than promising nothing.
	 *
	 * Takes a Viewer rather than an activation context because nothing has activated: this is a question about a
	 * player, asked ahead of time. A scaling modifier reads the Viewer here and the Instigator when granting; usually
	 * the same actor, and where they differ each path is asking about the right one.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, meta = (BlueprintProtected = "true"), Category = "Reward")
	bool ModifyPreview(UPARAM(ref) FQuestRewardPreview& Preview, AActor* Viewer);
};

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
	virtual bool ModifyPreview_Implementation(FQuestRewardPreview& Preview, AActor* Viewer) override;

	/**
	 * The whole authoring surface for the common case: given the amount, return the new one. Return zero or less to
	 * drop the grant - a reward worth nothing is worth not publishing. Incoming is here because the reference case
	 * needs it: scaling reads the recipient's own state off the activation Instigator.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, meta = (BlueprintProtected = "true"), Category = "Reward")
	int32 ModifyAmount(int32 Amount, const FQuestRewardActivationContext& Incoming);
};

/**
 * Reference modifier: clamps an amount into [MinAmount, MaxAmount]. Deliberately the simplest useful one - it exists to
 * demonstrate the layer and to give the order-matters case something concrete to test with, since scale-then-clamp and
 * clamp-then-scale produce different numbers from the same two modifiers.
 */
UCLASS(meta = (DisplayName = "Clamp Amount"))
class SIMPLEQUEST_API UClampAmountModifier : public UQuestRewardAmountModifier
{
	GENERATED_BODY()

	/** Tests author the bounds the way a designer would; see FQuestRewardModifierTestAccess. */
	friend class FQuestRewardModifierTestAccess;

protected:
	/** Lower bound. A grant below this is raised to it; set both bounds equal to force a fixed amount. */
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0"), Category = "Modifier")
	int32 MinAmount = 0;

	/** Upper bound. A grant above this is lowered to it. */
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0"), Category = "Modifier")
	int32 MaxAmount = 1000;

	virtual int32 ModifyAmount_Implementation(int32 Amount, const FQuestRewardActivationContext& Incoming) override;
};

/**
 * Reference modifier: multiplies an amount by the recipient's own scale, read through IRewardScalingSource. This is the
 * transform half of what Scaled Amount Reward used to do inside one class - and unlike that class it composes with any
 * source, so scaled loot is a loot table with this attached rather than a class nobody has written.
 *
 * An actor that does not implement the interface scales by one, so attaching this is safe before the game supplies a
 * value.
 */
UCLASS(meta = (DisplayName = "Scale By Recipient"))
class SIMPLEQUEST_API UScaleByRecipientModifier : public UQuestRewardAmountModifier
{
	GENERATED_BODY()

protected:
	virtual int32 ModifyAmount_Implementation(int32 Amount, const FQuestRewardActivationContext& Incoming) override;
};

