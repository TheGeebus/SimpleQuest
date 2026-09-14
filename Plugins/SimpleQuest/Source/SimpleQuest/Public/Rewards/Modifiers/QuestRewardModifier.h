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

	/** Advertisement twin of DispatchModifyGrant. Annotates rather than deciding - see ModifyPreview. */
	void DispatchModifyPreview(FQuestRewardPreview& Preview, const FQuestRewardActivationContext& AsIfActivating);

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
	 * Transform what the reward ADVERTISES, and record anything standing in the way of it being granted.
	 *
	 * *** THIS DOES NOT DECIDE WHETHER THE REWARD IS SHOWN. *** ModifyGrant returns a verdict because a grant is a
	 * decision; a preview is a DESCRIPTION, so a modifier that would drop the grant adds a blocker via AddBlocker
	 * instead of suppressing the entry. Hiding an unavailable reward is a presentation choice that belongs to the UI,
	 * which can then render "50 XP - already collected" rather than showing nothing at all.
	 *
	 * A useful consequence: describing the present needs no prediction. Asking whether a reward WOULD be granted after
	 * a completion that has not happened is a question a modifier generally cannot answer; saying what blocks it right
	 * now is one it always can.
	 *
	 * TAKES A CONTEXT RATHER THAN A VIEWER, symmetric with ModifyGrant: nothing has activated, so Provenance stays
	 * Unknown, but Instigator carries the VIEWER and ResolvingQuestTag the same quest the grant path names.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, meta = (BlueprintProtected = "true"), Category = "Reward")
	void ModifyPreview(UPARAM(ref) FQuestRewardPreview& Preview, const FQuestRewardActivationContext& AsIfActivating);

	/** Records a reason this reward would not currently be granted. Several modifiers may each add their own. */
	UFUNCTION(BlueprintCallable, meta = (BlueprintProtected = "true"), Category = "Reward")
	static void AddBlocker(UPARAM(ref) FQuestRewardPreview& Preview, FGameplayTag BlockerType, const FText& Description);

	/**
	 * The GameInstance a grant belongs to, reached through whoever the grant is about. A modifier is a subobject of a
	 * reward on a questline graph ASSET, so its own GetWorld() is null and this walk is the only route to the live game -
	 * which is why it lives here once rather than in each modifier that needs a subsystem.
	 *
	 * Null when the Instigator is gone or carries no world. Callers decide what that means for them; a grant and a
	 * preview do not want the same answer.
	 */
	static const UGameInstance* FindGameInstanceForGrant(const FQuestRewardActivationContext& Context);

	/**
	 * Resolutions of the context's quest AS OF THE COMPLETION THIS CALL IS ABOUT - the shared reference point that lets
	 * a grant and its own advertisement compare identically.
	 *
	 * *** A PREVIEW IS A LOOKAHEAD. *** It answers "given this completion, what do you get," so it must count the
	 * completion it describes. A grant runs with its own resolution already recorded and so is already correct; a
	 * preview is asked beforehand and is one behind, and the pending completion is added here. Getting this wrong is
	 * precisely the off-by-one that makes an advertisement promise what the grant then refuses.
	 *
	 * INDEX_NONE when it cannot be established - no resolving quest on the context, or no reachable state subsystem.
	 * Silent, because a grant wants a warning and a preview asked every frame by a tooltip does not.
	 */
	int32 GetCompletionCount(const FQuestRewardActivationContext& Context, bool bThisCompletionAlreadyCounted) const;
};

