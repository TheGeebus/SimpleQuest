// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "StructUtils/InstancedStruct.h"
#include "Quests/Types/QuestRewardActivationContext.h"
#include "Quests/Types/QuestRewardContext.h"
#include "Quests/Types/QuestRewardPreview.h"
#include "QuestRewardBase.generated.h"

class UQuestRewardModifier;

/**
 * Base class for a reward - the adapter between "quest flow reached this point" and "apply this effect to a recipient."
 * The framework doesn't define what a reward IS: subclass (C++ or Blueprint) and override TryGrantReward to compute
 * loot / XP / currency / anything, pack it into a payload struct, and call DeliverReward. Abstract - every reward is a
 * concrete subclass. For the no-code path (configure a type + a raw payload struct, no subclass), use UGenericReward.
 *
 * The base carries one authoring surface of its own: Modifiers, the transforms applied to whatever this reward
 * delivers. A reward decides WHAT to grant; a modifier changes it on the way out. See UQuestRewardModifier.
 *
 * EditInlineNew + Blueprintable: rewards are authored inline as an Instanced array on a reward node (each entry a
 * configured instance), and a designer can subclass in Blueprint to add typed fields + logic with no C++.
 */
UCLASS(Abstract, Blueprintable, EditInlineNew)
class SIMPLEQUEST_API UQuestRewardBase : public UObject
{
	GENERATED_BODY()

	/** Modifier tests reach in for Modifiers. */
	friend class FQuestRewardModifierTestAccess;

public:
	/**
	 * Node-facing entry point. Thin C++ forwarder to the protected BlueprintNativeEvent TryGrantReward - routes through
	 * the engine's UFunction thunk so Blueprint overrides fire. The reward node calls this on activation, then drains
	 * the queued deliveries via TakePendingGrants.
	 */
	void DispatchTryGrantReward(const FQuestRewardActivationContext& Incoming);
	
	/** Public C++ forwarder (thunk-routes to the BP-native event so subclass overrides fire). Mirrors DispatchTryGrantReward. */
	TArray<FQuestRewardPreview> DispatchDescribeReward(AActor* Viewer) const;

	/** Drains the grants queued by DeliverReward during the last TryGrantReward. The reward node finalizes + publishes each. */
	TArray<FQuestRewardContext> TakePendingGrants() { return MoveTemp(PendingGrants); }

	/**
	 * Runs this reward's modifiers over one drained grant, in array order. Returns FALSE when a modifier dropped it, in
	 * which case the caller publishes nothing.
	 *
	 * Lives here rather than in the reward node because the payload gate and the ordering belong next to the data that
	 * declares them - and because both grant paths (the node, and the manager for questline-level rewards) go through
	 * one implementation rather than two that can drift.
	 */
	bool ApplyModifiers(FQuestRewardContext& Grant, const FQuestRewardActivationContext& Incoming) const;

#if WITH_EDITOR
	/**
	 * Editor diagnostic: why this reward, as configured, should stop being used - or an empty string when there is
	 * nothing to say. The questline compiler asks every compiled reward and reports whatever comes back, so a reward
	 * on its way out declares that ITSELF and the compiler never carries a list of class names.
	 *
	 * Return a fragment that completes "A <Reward Name> on <where> ___", without trailing punctuation.
	 */
	virtual FString DescribeDeprecation() const { return FString(); }
#endif

	/**
	 * STABLE PER-INSTANCE IDENTITY, the same job UQuestlineNodeBase::QuestGuid does for a graph node. A reward is
	 * addressed by the data pipeline as a child row, and until this existed that row was keyed by the reward's ARRAY
	 * INDEX - a locally-minted, densely-packed key. Two people each appending a reward to the same array both mint the
	 * same next index, in different per-class files where a text merge sees no overlap at all.
	 *
	 * NOT EditAnywhere: identity is not authoring surface. Keeping it out of the edit set also keeps it out of the
	 * export's cells and the compiled dump, both of which walk editable properties - it addresses the row rather than
	 * being data on it.
	 */
	UPROPERTY()
	FGuid RewardGuid;

	virtual void PostLoad() override;
	virtual void PreSave(FObjectPreSaveContext SaveContext) override;

protected:
	/**
	 * Transforms applied to every grant this reward delivers, in ARRAY ORDER - stated rather than emergent, because
	 * scale-then-clamp and clamp-then-scale are different numbers. A cap of 100 listed BEFORE a doubling grants 200:
	 * the cap saw a number that had not been scaled yet. Put a ceiling last if that is what it is meant to be.
	 * Instanced, so each entry is its own configured instance and the compiler's deep copy carries them with the reward.
	 *
	 * Empty on almost every reward. A modifier is for the case a source cannot express: scaling loot, capping a
	 * placement, redirecting a recipient, or dropping a grant unless a condition holds.
	 */
	UPROPERTY(EditAnywhere, Instanced, Category = "Reward")
	TArray<TObjectPtr<UQuestRewardModifier>> Modifiers;

	/**
	 * Compute + deliver the grant(s). Fires when the reward node activates. Read Incoming ("how the flow reached me"),
	 * compute (roll loot, scale by context), and call DeliverReward - once, or multiple times for a multi-part grant.
	 * Declining (delivering nothing) is legal and never affects graph flow. The base is a no-op; every concrete reward
	 * overrides this (or uses UGenericReward's configured auto-deliver).
	 *
	 * BlueprintProtected: call via the public DispatchTryGrantReward from C++; subclass BPs override normally.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, meta = (BlueprintProtected = "true"), Category = "Reward")
	void TryGrantReward(const FQuestRewardActivationContext& Incoming);

	/**
	 * Preview hook - the reward's second verb, beside TryGrantReward. Returns display lines describing what this reward
	 * WOULD grant, WITHOUT granting: pure, no event, no chain. For "do this task, get this reward" UI. Return an EMPTY
	 * array to opt out of advertisement (a delivered-but-hidden reward). Association is compile-time; DESCRIPTION is
	 * query-time - read the Viewer to compute a live value. Base returns nothing; concrete rewards override.
	 *
	 * BlueprintProtected: call via the public DispatchDescribeReward from C++; subclass BPs override normally.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, meta = (BlueprintProtected = "true"), Category = "Reward")
	TArray<FQuestRewardPreview> DescribeReward(AActor* Viewer) const;
	
	/**
	 * Emit one grant - "send the struct out." Queues it for the reward node to finalize (fill lineage, default Recipient)
	 * and publish on the InRewardType channel. Call from within TryGrantReward. void - a grant's result never gates graph
	 * flow, and pub/sub means the publisher can't know its recipients.
	 *
	 * @param InRewardType  the kind granted (the publish channel; e.g. SimpleQuest.Reward.Currency.Gold)
	 * @param InPayload     the recipient-facing payload struct (rides FQuestRewardContext::CustomData)
	 * @param Recipient     optional explicit target; leave null to default to the activation Instigator
	 */
	UFUNCTION(BlueprintCallable, meta = (BlueprintProtected = "true", AutoCreateRefTerm = "InPayload"), Category = "Reward")
	void DeliverReward(FGameplayTag InRewardType, const FInstancedStruct& InPayload, AActor* Recipient = nullptr);

private:
	/**
	 * The advertisement twin, applied inside DispatchDescribeReward rather than by callers. Returns false when a
	 * modifier hid the preview.
	 */
	bool ApplyModifiersToPreview(FQuestRewardPreview& Preview, AActor* Viewer) const;
	
	/** Grants queued by DeliverReward during TryGrantReward; drained by the reward node via TakePendingGrants. */
	UPROPERTY()
	TArray<FQuestRewardContext> PendingGrants;
};