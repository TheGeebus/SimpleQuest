// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "StructUtils/InstancedStruct.h"
#include "Quests/Types/QuestRewardActivationContext.h"
#include "Quests/Types/QuestRewardContext.h"
#include "QuestRewardBase.generated.h"

/**
 * Base class for a reward — the adapter between "quest flow reached this point" and "apply this effect to a recipient."
 * The framework doesn't define what a reward IS: subclass (C++ or Blueprint) and override TryGrantReward to compute
 * loot / XP / currency / anything, pack it into a payload struct, and call DeliverReward. Abstract — the base carries
 * no authoring surface of its own; every reward is a concrete subclass. For the no-code path (configure a type + a raw
 * payload struct, no subclass), use UGenericReward.
 *
 * EditInlineNew + Blueprintable: rewards are authored inline as an Instanced array on a reward node (each entry a
 * configured instance), and a designer can subclass in Blueprint to add typed fields + logic with no C++.
 */
UCLASS(Abstract, Blueprintable, EditInlineNew)
class SIMPLEQUEST_API UQuestRewardBase : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Node-facing entry point. Thin C++ forwarder to the protected BlueprintNativeEvent TryGrantReward — routes through
	 * the engine's UFunction thunk so Blueprint overrides fire. The reward node calls this on activation, then drains
	 * the queued deliveries via TakePendingGrants.
	 */
	void DispatchTryGrantReward(const FQuestRewardActivationContext& Incoming);

	/** Drains the grants queued by DeliverReward during the last TryGrantReward. The reward node finalizes + publishes each. */
	TArray<FQuestRewardContext> TakePendingGrants() { return MoveTemp(PendingGrants); }

protected:
	/**
	 * Compute + deliver the grant(s). Fires when the reward node activates. Read Incoming ("how the flow reached me"),
	 * compute (roll loot, scale by context), and call DeliverReward — once, or multiple times for a multi-part grant.
	 * Declining (delivering nothing) is legal and never affects graph flow. The base is a no-op; every concrete reward
	 * overrides this (or uses UGenericReward's configured auto-deliver).
	 *
	 * BlueprintProtected: call via the public DispatchTryGrantReward from C++; subclass BPs override normally.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, meta = (BlueprintProtected = "true"), Category = "Reward")
	void TryGrantReward(const FQuestRewardActivationContext& Incoming);

	/**
	 * Emit one grant — "send the struct out." Queues it for the reward node to finalize (fill lineage, default Recipient)
	 * and publish on the InRewardType channel. Call from within TryGrantReward. void — a grant's result never gates graph
	 * flow, and pub/sub means the publisher can't know its recipients.
	 *
	 * @param InRewardType  the kind granted (the publish channel; e.g. SimpleQuest.Reward.Currency.Gold)
	 * @param InPayload     the recipient-facing payload struct (rides FQuestRewardContext::CustomData)
	 * @param Recipient     optional explicit target; leave null to default to the activation Instigator
	 */
	UFUNCTION(BlueprintCallable, meta = (BlueprintProtected = "true", AutoCreateRefTerm = "InPayload"), Category = "Reward")
	void DeliverReward(FGameplayTag InRewardType, const FInstancedStruct& InPayload, AActor* Recipient = nullptr);

private:
	/** Grants queued by DeliverReward during TryGrantReward; drained by the reward node via TakePendingGrants. */
	UPROPERTY()
	TArray<FQuestRewardContext> PendingGrants;
};