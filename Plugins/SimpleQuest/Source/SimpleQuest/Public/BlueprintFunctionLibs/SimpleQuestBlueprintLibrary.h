// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Kismet/BlueprintFunctionLibrary.h"
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

};
