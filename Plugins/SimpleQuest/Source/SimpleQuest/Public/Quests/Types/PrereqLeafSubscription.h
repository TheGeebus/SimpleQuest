// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Delegates/Delegate.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameplayTagContainer.h"
#include "Quests/Types/PrerequisiteExpression.h"
#include "Signals/SignalSubsystem.h"
#include "Events/QuestEntryRecordedEvent.h"
#include "Events/QuestResolutionRecordedEvent.h"
#include "WorldState/WorldStateSubsystem.h"

namespace FPrereqLeafSubscription
{
	/**
	 * Per-channel subscription handles. One named slot per event type so multiple leaf kinds keyed on the same
	 * channel — most commonly a Leaf_Resolution and a Leaf_Entry both referencing the same source quest tag —
	 * can each hold their own subscription without one overwriting the other. Slots not populated by a given
	 * leaf kind stay default-invalid and are skipped during cleanup. Replaces the prior FPrereqLeafHandlePair
	 * (Added + Removed) which conflated event types behind a generic "Added" name.
	 */
	struct FPrereqLeafHandles
	{
		FDelegateHandle FactAdded;       // Leaf, both overloads
		FDelegateHandle FactRemoved;     // Leaf, symmetric overload only
		FDelegateHandle Resolution;      // Leaf_Resolution
		FDelegateHandle Entry;           // Leaf_Entry
	};

	/**
	 * Unsubscribes every populated slot in Handles and clears the map. Replaces the inline iterate-and-
	 * unsubscribe pattern at every consumer cleanup site so the per-slot IsValid guards stay consistent.
	 */
	inline void UnsubscribeAll(USignalSubsystem* Signals, TMap<FGameplayTag, FPrereqLeafHandles>& Handles)
	{
		if (Signals)
		{
			for (auto& Pair : Handles)
			{
				if (Pair.Value.FactAdded.IsValid())   Signals->UnsubscribeMessage(Pair.Key, Pair.Value.FactAdded);
				if (Pair.Value.FactRemoved.IsValid()) Signals->UnsubscribeMessage(Pair.Key, Pair.Value.FactRemoved);
				if (Pair.Value.Resolution.IsValid())  Signals->UnsubscribeMessage(Pair.Key, Pair.Value.Resolution);
				if (Pair.Value.Entry.IsValid())       Signals->UnsubscribeMessage(Pair.Key, Pair.Value.Entry);
			}
		}
		Handles.Reset();
	}

	namespace Detail
	{
		/**
		 * Internal: both public overloads forward here. Resolves USignalSubsystem from Subscriber's world chain
		 * (Subscriber must be a UObject with a working GetWorld(); UQuestNodeBase satisfies this via its GetWorld
		 * override that routes through CachedGameInstance, UQuestManagerSubsystem satisfies it directly as a
		 * UGameInstanceSubsystem). Iterates Expr's leaves, per-event-type dedupes per channel, calls the type-
		 * appropriate SubscribeMessage on the resolved SignalSubsystem, invokes StoreHandles per newly-subscribed
		 * channel.
		 *
		 * Per-event-type dedupe: a channel can be hit once per event type (Fact / Resolution / Entry) within a
		 * single expression. Multiple leaves of the SAME type on the SAME channel collapse to one subscription:
		 * the handler re-evaluates the full expression on any event, which checks each leaf internally via the
		 * type-appropriate Has* call. Mixed types on the same channel each get their own subscription, stored in
		 * distinct slots on the per-channel FPrereqLeafHandles record.
		 */
		template<typename T>
		void SubscribeImpl(
			const FPrerequisiteExpression& Expr,
			T* Subscriber,
			void (T::*FactAddedHandler)(FGameplayTag, const FWorldStateFactAddedEvent&),
			void (T::*FactRemovedHandler)(FGameplayTag, const FWorldStateFactRemovedEvent&),
			void (T::*ResolutionHandler)(FGameplayTag, const FQuestResolutionRecordedEvent&),
			void (T::*EntryHandler)(FGameplayTag, const FQuestEntryRecordedEvent&),
			TFunctionRef<void(FGameplayTag Channel, const FPrereqLeafHandles& Slot)> StoreHandles)
		{
			if (!Subscriber) return;
			const UWorld* World = Subscriber->GetWorld();
			UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
			USignalSubsystem* Signals = GameInstance ? GameInstance->GetSubsystem<USignalSubsystem>() : nullptr;
			if (!Signals) return;

			TArray<FPrereqLeafDescriptor> Leaves;
			Expr.CollectLeaves(Leaves);

			TSet<FGameplayTag> SubscribedFactChannels;
			TSet<FGameplayTag> SubscribedResolutionChannels;
			TSet<FGameplayTag> SubscribedEntryChannels;

			for (const FPrereqLeafDescriptor& Leaf : Leaves)
			{
				if (Leaf.Type == EPrerequisiteExpressionType::Leaf)
				{
					const FGameplayTag& Channel = Leaf.FactTag;
					if (!Channel.IsValid() || SubscribedFactChannels.Contains(Channel)) continue;

					FPrereqLeafHandles Slot;
					Slot.FactAdded = Signals->template SubscribeMessage<FWorldStateFactAddedEvent>(Channel, Subscriber, FactAddedHandler);
					if (FactRemovedHandler)
					{
						Slot.FactRemoved = Signals->template SubscribeMessage<FWorldStateFactRemovedEvent>(Channel, Subscriber, FactRemovedHandler);
					}
					StoreHandles(Channel, Slot);
					SubscribedFactChannels.Add(Channel);
				}
				else if (Leaf.Type == EPrerequisiteExpressionType::Leaf_Resolution)
				{
					const FGameplayTag& Channel = Leaf.LeafQuestTag;
					if (!Channel.IsValid() || SubscribedResolutionChannels.Contains(Channel)) continue;

					FPrereqLeafHandles Slot;
					Slot.Resolution = Signals->template SubscribeMessage<FQuestResolutionRecordedEvent>(Channel, Subscriber, ResolutionHandler);
					StoreHandles(Channel, Slot);
					SubscribedResolutionChannels.Add(Channel);
				}
				else if (Leaf.Type == EPrerequisiteExpressionType::Leaf_Entry)
				{
					const FGameplayTag& Channel = Leaf.LeafQuestTag;
					if (!Channel.IsValid() || SubscribedEntryChannels.Contains(Channel)) continue;

					FPrereqLeafHandles Slot;
					Slot.Entry = Signals->template SubscribeMessage<FQuestEntryRecordedEvent>(Channel, Subscriber, EntryHandler);
					StoreHandles(Channel, Slot);
					SubscribedEntryChannels.Add(Channel);
				}
			}
		}

		/** Merges NewSlot into Existing, preserving any handles already populated on Existing. Used by the public
		 *  overloads' StoreHandles lambdas so a channel hit by multiple leaf types accumulates handles across calls. */
		inline void MergeSlot(FPrereqLeafHandles& Existing, const FPrereqLeafHandles& NewSlot)
		{
			if (NewSlot.FactAdded.IsValid())   Existing.FactAdded   = NewSlot.FactAdded;
			if (NewSlot.FactRemoved.IsValid()) Existing.FactRemoved = NewSlot.FactRemoved;
			if (NewSlot.Resolution.IsValid())  Existing.Resolution  = NewSlot.Resolution;
			if (NewSlot.Entry.IsValid())       Existing.Entry       = NewSlot.Entry;
		}
	}

	/**
	 * Monotonic overload: for sites where leaves only need re-evaluation on Added (no NOT-expression flips that
	 * would require symmetric Add/Remove subscription). Used by UQuestNodeBase::DeferActivation,
	 * UQuestPrereqRuleNode::Activate, and UQuestManagerSubsystem::DeferChainToNextNodes.
	 *
	 * Stores handles in a TMap<FGameplayTag, FPrereqLeafHandles> per channel. FactRemoved stays default-invalid.
	 */
	template<typename T>
	void SubscribeLeavesForReevaluation(
		const FPrerequisiteExpression& Expr,
		T* Subscriber,
		void (T::*FactAddedHandler)(FGameplayTag, const FWorldStateFactAddedEvent&),
		void (T::*ResolutionHandler)(FGameplayTag, const FQuestResolutionRecordedEvent&),
		void (T::*EntryHandler)(FGameplayTag, const FQuestEntryRecordedEvent&),
		TMap<FGameplayTag, FPrereqLeafHandles>& OutHandles)
	{
		// Cast nullptr to the typed method-pointer so template deduction can resolve T inside SubscribeImpl;
		// SubscribeImpl's runtime `if (FactRemovedHandler)` branch correctly skips the FactRemoved subscribe
		// when the typed-null is passed in.
		using FactRemovedHandlerType = void (T::*)(FGameplayTag, const FWorldStateFactRemovedEvent&);
		Detail::SubscribeImpl(Expr, Subscriber,
			FactAddedHandler, static_cast<FactRemovedHandlerType>(nullptr), ResolutionHandler, EntryHandler,
			[&OutHandles](FGameplayTag Channel, const FPrereqLeafHandles& Slot)
			{
				Detail::MergeSlot(OutHandles.FindOrAdd(Channel), Slot);
			});
	}

	/**
	 * Symmetric overload: for sites where NOT-expressions can flip a satisfied Leaf back to unsatisfied when the
	 * underlying fact is removed. Currently only UQuestManagerSubsystem::RegisterEnablementWatch needs this
	 * (FQuestEnabledEvent / FQuestDisabledEvent bidirectional UI sync requires it).
	 *
	 * Stores handles in the same TMap<FGameplayTag, FPrereqLeafHandles> shape. FactRemoved is populated on Leaf
	 * channels; Resolution and Entry have no symmetric "removed" event (both registries are append-only).
	 */
	template<typename T>
	void SubscribeLeavesForReevaluation(
		const FPrerequisiteExpression& Expr,
		T* Subscriber,
		void (T::*FactAddedHandler)(FGameplayTag, const FWorldStateFactAddedEvent&),
		void (T::*FactRemovedHandler)(FGameplayTag, const FWorldStateFactRemovedEvent&),
		void (T::*ResolutionHandler)(FGameplayTag, const FQuestResolutionRecordedEvent&),
		void (T::*EntryHandler)(FGameplayTag, const FQuestEntryRecordedEvent&),
		TMap<FGameplayTag, FPrereqLeafHandles>& OutHandles)
	{
		Detail::SubscribeImpl(Expr, Subscriber,
			FactAddedHandler, FactRemovedHandler, ResolutionHandler, EntryHandler,
			[&OutHandles](FGameplayTag Channel, const FPrereqLeafHandles& Slot)
			{
				Detail::MergeSlot(OutHandles.FindOrAdd(Channel), Slot);
			});
	}
}