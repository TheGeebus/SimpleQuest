// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Quests/Types/QuestRewardActivationContext.h"
#include "Quests/Types/QuestRewardContext.h"
#include "Quests/Types/QuestRewardPreview.h"
#include "QuestRewardModifier.generated.h"


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
	bool DispatchModifyPreview(FQuestRewardPreview& Preview, const FQuestRewardActivationContext& AsIfActivating);

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
	 * TAKES A CONTEXT RATHER THAN A VIEWER, and the symmetry with ModifyGrant is the point: whatever a modifier can
	 * branch on while granting, it can branch on while advertising. Nothing has activated, so Provenance stays Unknown
	 * and no outcome routed here; Instigator carries the VIEWER (a scaling modifier reads it exactly as it reads the
	 * Instigator when granting), and ResolvingQuestTag carries the same quest the grant path names.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, meta = (BlueprintProtected = "true"), Category = "Reward")
	bool ModifyPreview(UPARAM(ref) FQuestRewardPreview& Preview, const FQuestRewardActivationContext& AsIfActivating);
};

