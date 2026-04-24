// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Signals/SignalSubsystem.h"
#include "Utilities/QuestStateTagUtils.h"
#include "SimpleQuestBlueprintLibrary.generated.h"

UCLASS()
class SIMPLEQUEST_API USimpleQuestBlueprintLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:

    // -------------------------------------------------------------------------
    // Quest state queries — read directly from WorldState
    // -------------------------------------------------------------------------

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimpleQuest|Quest State", meta = (WorldContext = "WorldContext"))
    static bool IsQuestActive(const UObject* WorldContext, FGameplayTag QuestTag);

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimpleQuest|Quest State", meta = (WorldContext = "WorldContext"))
    static bool IsQuestCompleted(const UObject* WorldContext, FGameplayTag QuestTag);

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimpleQuest|Quest State", meta = (WorldContext = "WorldContext"))
    static bool IsQuestPendingGiver(const UObject* WorldContext, FGameplayTag QuestTag);

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimpleQuest|Quest State", meta = (WorldContext = "WorldContext"))
    static bool IsQuestResolvedWith(const UObject* WorldContext, FGameplayTag QuestTag, FGameplayTag OutcomeTag);

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimpleQuest|Quest State", meta = (WorldContext = "WorldContext"))
    static int32 GetQuestCompletionCount(const UObject* WorldContext, FGameplayTag QuestTag);

    // -------------------------------------------------------------------------
    // Quest actions — publish to the signal bus; designer never touches the bus
    // -------------------------------------------------------------------------

    UFUNCTION(BlueprintCallable, Category = "SimpleQuest|Quest Actions", meta = (WorldContext = "WorldContext"))
    static void AbandonQuest(const UObject* WorldContext, FGameplayTag QuestTag);

    UFUNCTION(BlueprintCallable, Category = "SimpleQuest|Quest Actions", meta = (WorldContext = "WorldContext"))
    static void GiveQuest(const UObject* WorldContext, FGameplayTag QuestTag);

    // -------------------------------------------------------------------------
    // World state — general fact store, for power users and external prereqs
    // -------------------------------------------------------------------------

    UFUNCTION(BlueprintCallable, Category = "SimpleQuest|World State", meta = (WorldContext = "WorldContext"))
    static void AddWorldStateFact(const UObject* WorldContext, FGameplayTag FactTag);

    UFUNCTION(BlueprintCallable, Category = "SimpleQuest|World State", meta = (WorldContext = "WorldContext"))
    static void RemoveWorldStateFact(const UObject* WorldContext, FGameplayTag FactTag);

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimpleQuest|World State", meta = (WorldContext = "WorldContext"))
    static bool HasWorldStateFact(const UObject* WorldContext, FGameplayTag FactTag);

private:
    static class UWorldStateSubsystem* GetWorldState(const UObject* WorldContext);
    static class USignalSubsystem* GetSignalSubsystem(const UObject* WorldContext);
    static class UQuestManagerSubsystem* GetQuestManager(const UObject* WorldContext);

public:
    
    /**
     * C++ one-liner for subscribing to a quest event. Resolves the SignalSubsystem from the world context, subscribes
     * the listener/callback on QuestTag, returns the FDelegateHandle for explicit unbind. Returns an invalid handle if
     * the subsystem can't be resolved or the tag isn't registered — same silent-failure contract as the BP async action.
     *
     * TEvent must be a FQuestEventBase-derived struct published on the quest's tag channel — FQuestStartedEvent,
     * FQuestEndedEvent, FQuestEnabledEvent, FQuestDeactivatedEvent, etc.
     */
    template<typename TEvent, typename TObject>
    static FDelegateHandle SubscribeToQuestEvent(UObject* WorldContextObject, const FGameplayTag& QuestTag, TObject* Listener,
        void (TObject::* Callback)(FGameplayTag, const TEvent&))
    {
        if (!FQuestStateTagUtils::IsTagRegisteredInRuntime(QuestTag)) return FDelegateHandle();
        if (USignalSubsystem* Signals = GetSignalSubsystem(WorldContextObject))
        {
            return Signals->SubscribeMessage<TEvent>(QuestTag, Listener, Callback);
        }
        return FDelegateHandle();
    }

    /** Companion unbind — pairs with BindToQuestEvent's returned handle. Safe no-op if the handle is invalid. */
    static void UnsubscribeFromQuestEvent(UObject* WorldContextObject, const FGameplayTag& QuestTag, FDelegateHandle Handle);

};
