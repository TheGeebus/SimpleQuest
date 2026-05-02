// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Delegates/Delegate.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameplayTagContainer.h"
#include "Quests/Types/PrerequisiteExpression.h"
#include "Signals/SignalSubsystem.h"
#include "Events/QuestResolutionRecordedEvent.h"
#include "WorldState/WorldStateSubsystem.h"

namespace PrereqLeafSubscription
{
	/**
	 * Per-channel handle pair for symmetric-subscription sites (RegisterEnablementWatch). RemovedHandle is invalid
	 * for Leaf_Resolution channels (resolutions are append-only, no symmetric removed event) and for Leaf channels
	 * when the symmetric-subscribe overload was not used.
	 */
	struct FPrereqLeafHandlePair
	{
		FDelegateHandle AddedHandle;
		FDelegateHandle RemovedHandle;
	};

	namespace Detail
	{
		/**
		 * Internal: both public overloads forward here. Resolves USignalSubsystem from Subscriber's world chain
		 * (Subscriber must be a UObject with a working GetWorld(); UQuestNodeBase satisfies this via its GetWorld
		 * override that routes through CachedGameInstance, UQuestManagerSubsystem satisfies it directly as a
		 * UGameInstanceSubsystem). Iterates Expr's leaves, dedupes per channel, calls the type-appropriate
		 * SubscribeMessage on the resolved SignalSubsystem, invokes StoreHandles per newly-subscribed channel.
		 *
		* Per-channel dedupe: multiple leaves with the same channel only subscribe once. The handler re-evaluates
		 * the full expression on any event, which checks each leaf internally. Safe for resolution leaves whose
		 * outcome tags differ but whose LeafQuestTag is the same.
		 */
		template<typename T>
		void SubscribeImpl(
			const FPrerequisiteExpression& Expr,
			T* Subscriber,
			void (T::*FactAddedHandler)(FGameplayTag, const FWorldStateFactAddedEvent&),
			void (T::*FactRemovedHandler)(FGameplayTag, const FWorldStateFactRemovedEvent&),
			void (T::*ResolutionHandler)(FGameplayTag, const FQuestResolutionRecordedEvent&),
			TFunctionRef<void(FGameplayTag Channel, FDelegateHandle Added, FDelegateHandle Removed)> StoreHandles)
		{
			if (!Subscriber) return;
			const UWorld* World = Subscriber->GetWorld();
			UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
			USignalSubsystem* Signals = GameInstance ? GameInstance->GetSubsystem<USignalSubsystem>() : nullptr;
			if (!Signals) return;

			TArray<FPrereqLeafDescriptor> Leaves;
			Expr.CollectLeaves(Leaves);

			TSet<FGameplayTag> AlreadySubscribed;
			for (const FPrereqLeafDescriptor& Leaf : Leaves)
			{
				if (Leaf.Type == EPrerequisiteExpressionType::Leaf)
				{
					const FGameplayTag& Channel = Leaf.FactTag;
					if (!Channel.IsValid() || AlreadySubscribed.Contains(Channel)) continue;

					FDelegateHandle Added = Signals->SubscribeMessage<FWorldStateFactAddedEvent>(Channel, Subscriber, FactAddedHandler);
					FDelegateHandle Removed;
					if (FactRemovedHandler)
					{
						Removed = Signals->SubscribeMessage<FWorldStateFactRemovedEvent>(Channel, Subscriber, FactRemovedHandler);
					}
					StoreHandles(Channel, Added, Removed);
					AlreadySubscribed.Add(Channel);
				}
				else if (Leaf.Type == EPrerequisiteExpressionType::Leaf_Resolution)
				{
					const FGameplayTag& Channel = Leaf.LeafQuestTag;
					if (!Channel.IsValid() || AlreadySubscribed.Contains(Channel)) continue;

					FDelegateHandle Added = Signals->template SubscribeMessage<FQuestResolutionRecordedEvent>(Channel, Subscriber, ResolutionHandler);
					StoreHandles(Channel, Added, FDelegateHandle{});
					AlreadySubscribed.Add(Channel);
				}
			}
		}
	}

	/**
	 * Monotonic overload: for sites where leaves only need re-evaluation on Added (no NOT-expression flips that
	 * would require symmetric Add/Remove subscription). Used by UQuestNodeBase::DeferActivation,
	 * UQuestPrereqRuleNode::Activate, and UQuestManagerSubsystem::DeferChainToNextNodes.
	 *
	 * Stores one FDelegateHandle per channel - the AddedHandle. RemovedHandle is implicitly absent.
	 */
	template<typename T>
	void SubscribeLeavesForReevaluation(
		const FPrerequisiteExpression& Expr,
		T* Subscriber,
		void (T::*FactAddedHandler)(FGameplayTag, const FWorldStateFactAddedEvent&),
		void (T::*ResolutionHandler)(FGameplayTag, const FQuestResolutionRecordedEvent&),
		TMap<FGameplayTag, FDelegateHandle>& OutHandles)
	{
		// Cast nullptr to the typed method-pointer so template deduction can resolve T inside SubscribeImpl;
		// SubscribeImpl's runtime `if (FactRemovedHandler)` branch correctly skips the FactRemoved subscribe
		// when the typed-null is passed in.
		using FactRemovedHandlerType = void (T::*)(FGameplayTag, const FWorldStateFactRemovedEvent&);
		Detail::SubscribeImpl(Expr, Subscriber,
			FactAddedHandler, static_cast<FactRemovedHandlerType>(nullptr), ResolutionHandler,
			[&OutHandles](FGameplayTag Channel, FDelegateHandle Added, FDelegateHandle Removed)
			{
				OutHandles.Add(Channel, Added);
			});
	}

	/**
	 * Symmetric overload: for sites where NOT-expressions can flip a satisfied Leaf back to unsatisfied when the
	 * underlying fact is removed. Currently only UQuestManagerSubsystem::RegisterEnablementWatch needs this
	 * (FQuestEnabledEvent / FQuestDisabledEvent bidirectional UI sync requires it).
	 *
	 * Stores both AddedHandle and RemovedHandle per channel. RemovedHandle is invalid on Leaf_Resolution channels
	 * (resolutions are append-only).
	 */
	template<typename T>
	void SubscribeLeavesForReevaluation(
		const FPrerequisiteExpression& Expr,
		T* Subscriber,
		void (T::*FactAddedHandler)(FGameplayTag, const FWorldStateFactAddedEvent&),
		void (T::*FactRemovedHandler)(FGameplayTag, const FWorldStateFactRemovedEvent&),
		void (T::*ResolutionHandler)(FGameplayTag, const FQuestResolutionRecordedEvent&),
		TMap<FGameplayTag, FPrereqLeafHandlePair>& OutHandles)
	{
		Detail::SubscribeImpl(Expr, Subscriber,
			FactAddedHandler, FactRemovedHandler, ResolutionHandler,
			[&OutHandles](FGameplayTag Channel, FDelegateHandle Added, FDelegateHandle Removed)
			{
				OutHandles.Add(Channel, FPrereqLeafHandlePair{ Added, Removed });
			});
	}
}
