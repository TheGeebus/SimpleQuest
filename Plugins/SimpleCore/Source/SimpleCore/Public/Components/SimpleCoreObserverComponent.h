// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Components/ActorComponent.h"
#include "Signals/Types/SignalRoutingFlags.h"
#include "SimpleCoreObserverComponent.generated.h"


struct FWorldStateFactAddedEvent;
struct FWorldStateFactRemovedEvent;

class USignalSubsystem;
class UWorldStateSubsystem;


/**
 * Which of a watched fact's two transitions this entry reacts to, and how far down the tag hierarchy it listens.
 *
 * Facts have no kinds, so there is nothing to fan out into - a fact is asserted or it is not. That is why this is two
 * flags and a routing mode where a quest observer's equivalent carries eleven. The asymmetry is correct, and it is the
 * first thing you will notice if you have configured both.
 */
USTRUCT(BlueprintType)
struct FObservedFactSettings
{
	GENERATED_BODY()

	/**
	 * Fire ON FACT ADDED for this entry. The subscription also replays whatever is ALREADY true when this component
	 * registers, flagged as a catch-up delivery, so an actor spawning into a world where the power is already on hears
	 * about it rather than having to go and ask.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bObserveAdded = true;

	/**
	 * Fire ON FACT REMOVED for this entry. There is no catch-up on this side, and that is deliberate rather than
	 * missing: a fact store says what is true NOW, absence is the default condition of nearly every tag that could
	 * exist, and a removal that already happened is a moment that has passed.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bObserveRemoved = true;

	/**
	 * How far this entry listens. The default hears the exact tag OR anything beneath it, which is what makes "watch
	 * everything under MyGame.Weather" a single entry. Set it to ExactOnly when a parent tag is the only thing you
	 * mean and descendants would be noise.
	 *
	 * Routing matters more here than on a quest observer: a quest hierarchy is the framework's and its shape is known
	 * in advance, while a fact hierarchy is yours.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	ESignalRoutingMode Routing = FSignalRoutingDefaults::HierarchicalSubscribe;
};


/**
 * Watches world state facts for the actor it sits on, and tells that actor when they come true or stop being true.
 *
 * Drop it on anything that should react to world state - a door, a light, a spawner, a UI widget's owning actor. List
 * the fact tags you care about, bind ON FACT ADDED and ON FACT REMOVED, and that is the whole configuration. Any tag
 * is allowed: facts belong to whoever writes them, so a weather system's MyGame.Weather.Raining is as valid here as
 * anything a quest publishes, and nothing in this component knows the difference.
 *
 * It answers one question - IS THIS TRUE - and it answers it from the moment it registers rather than only from the
 * next change, because every fact already true is replayed to it on arrival. If what you need is "was this ever
 * true", that is a different question and it needs its own fact: write one when the thing happens, and watch that.
 */
UCLASS(ClassGroup=(SimpleCore), meta=(BlueprintSpawnableComponent))
class SIMPLECORE_API USimpleCoreObserverComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USimpleCoreObserverComponent();

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnFactAdded, FGameplayTag, FactTag, bool, bCatchUp);
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnFactRemoved, FGameplayTag, FactTag);

	/**
	 * Fires when a watched fact becomes true. FactTag is the fact itself and not the entry that matched it - watch
	 * MyGame.Weather, and a fact added at MyGame.Weather.Raining arrives as MyGame.Weather.Raining, which is the thing
	 * that actually happened. Every entry lands on this one pin, so branch on the tag when you watch more than one.
	 *
	 * bCatchUp is true when the delivery is a replay of something that was ALREADY true when this component
	 * registered. Read it as "this is already true", not "this just happened": skip one-shots like a sound or a
	 * flourish on those, and run state-setting logic either way.
	 */
	UPROPERTY(BlueprintAssignable, BlueprintCallable, Category="Facts")
	FOnFactAdded OnFactAdded;

	/** Fires when a watched fact stops being true. Never replayed - see bObserveRemoved for the reasoning. */
	UPROPERTY(BlueprintAssignable, BlueprintCallable, Category="Facts")
	FOnFactRemoved OnFactRemoved;

	/**
	 * Applies tag renames to authored tag containers that the editor-side reflection sweep cannot address - a TMap
	 * with FGameplayTag KEYS, which needs remove-then-readd because TMap does not permit key mutation. Plain
	 * FGameplayTag and FGameplayTagContainer properties are handled generically by the sweep and need no override.
	 * Returns the number of individual tag swaps performed.
	 *
	 * *** AN OVERRIDE MUST CHAIN TO Super AND ADD ITS OWN COUNT. *** This implementation heals ObservedFacts, so a
	 * subclass that replaces it without calling up silently stops healing the fact map on its own instances.
	 */
	virtual int32 ApplyTagRenames(const TMap<FName, FName>& Renames);

	/**
	 * Removes the listed tags from every authored tag container on this component. Called by the editor's stale-tag
	 * cleanup. Dirties the owning actor on the first removal so the change survives a save. Returns the number of
	 * individual removals performed.
	 *
	 * *** AN OVERRIDE MUST CHAIN TO Super AND ADD ITS OWN COUNT, *** for the same reason as ApplyTagRenames.
	 */
	virtual int32 RemoveTags(const TArray<FGameplayTag>& TagsToRemove);

	/**
	 * Subscribe every authored entry and take delivery of what is already true. Runs automatically one tick after
	 * BeginPlay; public for the rare owner that finishes its setup across several frames and wants to say when.
	 * Calling it twice is safe - each entry re-subscribes rather than stacking a second subscription.
	 */
	UFUNCTION(BlueprintCallable, Category="Facts")
	void RegisterFactObserver();

	/**
	 * Runtime: start watching a fact tag, or update an already-watched tag's settings. Once this component has
	 * registered, the tag subscribes and catches up immediately; before that, the entry is picked up by registration.
	 * Safe to call repeatedly for the same tag - it refreshes rather than double-subscribing.
	 */
	UFUNCTION(BlueprintCallable, Category="Facts")
	void AddObservedFact(FGameplayTag FactTag, FObservedFactSettings Settings);

	/** Runtime: stop watching a fact tag. Unsubscribes by stored handle. No-op for a tag that is not being watched. */
	UFUNCTION(BlueprintCallable, Category="Facts")
	void RemoveObservedFact(FGameplayTag FactTag);

	const TMap<FGameplayTag, FObservedFactSettings>& GetObservedFacts() const { return ObservedFacts; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/**
	 * Registration, deferred to the tick after BeginPlay so the owning actor finishes its own initialization - its
	 * Event BeginPlay, where it creates state and binds these delegates - before anything reaches this component.
	 * Virtual so a subclass can chain its own registration through Super.
	 */
	virtual void PerformDeferredRegistration();

	virtual void HandleFactAdded(FGameplayTag Channel, const FWorldStateFactAddedEvent& Event);
	virtual void HandleFactRemoved(FGameplayTag Channel, const FWorldStateFactRemovedEvent& Event);

	/** Subscribe ONE entry and catch it up, capturing its handles. Shared by registration and the runtime add path. */
	void RegisterSingleObservedFact(const FGameplayTag& FactTag, const FObservedFactSettings& Settings);

	/** Unsubscribe ONE fact entry and forget it. A thin wrapper over UnsubscribeHandlesForTag. */
	void UnregisterSingleObservedFact(const FGameplayTag& FactTag);

	/**
	 * Release every subscription recorded in Registry against Tag, and forget that bookkeeping. Returns how many
	 * handles were released, so a caller can tell "nothing was subscribed" from "something was, and now is not".
	 *
	 * The generic half of per-tag subscription lifetime, which is why it lives here: a subclass watching its own
	 * vocabulary needs exactly this behavior, and nothing about it is fact-shaped.
	 *
	 * *** THE REGISTRY IS A PARAMETER BECAUSE EACH WATCHING SURFACE OWNS ITS OWN. *** One shared map keyed by tag
	 * would mean a component watching one tag both as a fact and as something else released both at once - and
	 * dropping a fact entry would silently stop an unrelated subscription, with nothing in the log pointing at the
	 * cause. The framework's own vocabularies do not collide, but ObservedFacts deliberately accepts ANY tag, so
	 * that separation cannot rest on a naming convention the picker does not enforce.
	 */
	int32 UnsubscribeHandlesForTag(TMap<FGameplayTag, TArray<FDelegateHandle>>& Registry, const FGameplayTag& Tag);

	/**
	 * The fact entries' own subscription handles, keyed by the tag each was made against. Captured at subscribe time
	 * so ONE fact can be dropped without tearing the whole component down.
	 *
	 * Handles rather than Unsubscribe From Channel on purpose: a channel-wide removal would also take subscriptions
	 * this surface never made, and a handle is the only thing that identifies one binding.
	 *
	 * A subclass watching something else declares its OWN registry and passes it to UnsubscribeHandlesForTag - see
	 * that function for why they are kept apart. Each registry is emptied by the surface that owns it, in its own
	 * EndPlay; the bus side is already covered for all of them by the single UnsubscribeListener call.
	 */
	TMap<FGameplayTag, TArray<FDelegateHandle>> SubscriptionHandlesByFact;

	/**
	 * True once registration has run. Gates the live-versus-deferred branch in AddObservedFact / RemoveObservedFact:
	 * before registration, mutating the map is enough; after it, the mutation does live subscribe or unsubscribe work.
	 */
	bool bRegistered = false;

	UPROPERTY(VisibleInstanceOnly, Category="Facts")
	TObjectPtr<USignalSubsystem> SignalSubsystem;

private:
	UWorldStateSubsystem* GetWorldState() const;

	/**
	 * The facts this component watches, and what it wants from each. Deliberately carries NO tag category filter -
	 * facts are not the quest framework's, so the picker offers every tag your project defines.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Facts", meta=(AllowPrivateAccess=true))
	TMap<FGameplayTag, FObservedFactSettings> ObservedFacts;
};

