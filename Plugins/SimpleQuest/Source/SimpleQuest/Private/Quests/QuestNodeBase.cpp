// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Quests/QuestNodeBase.h"
#include "GameplayTagsManager.h"
#include "SimpleQuestLog.h"
#include "Signals/SignalSubsystem.h"
#include "WorldState/WorldStateSubsystem.h"

void UQuestNodeBase::Activate(FGameplayTag InContextualTag)
{
    UWorldStateSubsystem* WorldState = CachedGameInstance.IsValid() ? CachedGameInstance->GetSubsystem<UWorldStateSubsystem>() : nullptr;

    if (PrerequisiteExpression.IsAlways() || PrerequisiteExpression.Evaluate(WorldState))
    {
        ActivateInternal(InContextualTag);
        return;
    }
    DeferActivation(InContextualTag);
}

void UQuestNodeBase::ActivateInternal(FGameplayTag InContextualTag)
{
    SetContextualTag(InContextualTag);
    OnNodeStarted.ExecuteIfBound(this, InContextualTag);
}

void UQuestNodeBase::DeactivateInternal(FGameplayTag InContextualTag)
{
    // Cancel any deferred prereq subscriptions that are still live
    if (DeferredContextualTag.IsValid())
    {
        if (USignalSubsystem* Signals = CachedGameInstance.IsValid() ? CachedGameInstance->GetSubsystem<USignalSubsystem>() : nullptr)
        {
            for (auto& Pair : PrereqSubscriptionHandles)
            {
                Signals->UnsubscribeMessage(Pair.Key, Pair.Value);
            }
            PrereqSubscriptionHandles.Reset();
        }
        DeferredContextualTag = FGameplayTag::EmptyTag;
    }
}

void UQuestNodeBase::ForwardActivation()
{
    OnNodeForwardActivated.ExecuteIfBound(this);
}

void UQuestNodeBase::ResetTransientState()
{
    // Handles reference a SignalSubsystem from the previous PIE session — now dead. Clearing the map without
    // unsubscribing is safe: the owning subsystem is gone, there's nothing left to unsubscribe from.
    PrereqSubscriptionHandles.Reset();
    DeferredContextualTag = FGameplayTag::EmptyTag;
    ContextualTag = FGameplayTag::EmptyTag;
    bWasGiverGated = false;
    PendingActivationParams = FQuestObjectiveActivationParams{};
}

void UQuestNodeBase::DeferActivation(FGameplayTag InContextualTag)
{
    DeferredContextualTag = InContextualTag;

    USignalSubsystem* Signals = CachedGameInstance.IsValid() ? CachedGameInstance->GetSubsystem<USignalSubsystem>() : nullptr;
    if (!Signals) return;

    TArray<FGameplayTag> LeafTags;
    PrerequisiteExpression.CollectLeafTags(LeafTags);
    
    for (const FGameplayTag& LeafTag : LeafTags)
    {
        FDelegateHandle Handle = Signals->SubscribeMessage<FWorldStateFactAddedEvent>(LeafTag, this, &UQuestNodeBase::OnPrereqFactAdded);
        PrereqSubscriptionHandles.Add(LeafTag, Handle);
    }
}

void UQuestNodeBase::OnPrereqFactAdded(FGameplayTag Channel, const FWorldStateFactAddedEvent& Event)
{
    TryActivateDeferred();
}

void UQuestNodeBase::TryActivateDeferred()
{
    UWorldStateSubsystem* WorldState = CachedGameInstance.IsValid() ? CachedGameInstance->GetSubsystem<UWorldStateSubsystem>() : nullptr;
    if (!WorldState) return;

    if (!PrerequisiteExpression.Evaluate(WorldState)) return;

    if (USignalSubsystem* Signals = CachedGameInstance->GetSubsystem<USignalSubsystem>())
    {
        for (auto& Pair : PrereqSubscriptionHandles)
        {
            Signals->UnsubscribeMessage(Pair.Key, Pair.Value);
        }
        PrereqSubscriptionHandles.Reset();
    }

    const FGameplayTag TagToActivate = DeferredContextualTag;
    DeferredContextualTag = FGameplayTag::EmptyTag;
    ActivateInternal(TagToActivate);
}

void UQuestNodeBase::ResolveQuestTag(FName TagName)
{
    QuestTag = UGameplayTagsManager::Get().RequestGameplayTag(TagName, /*ErrorIfNotFound*/ false);
    NodeInfo.QuestTag = QuestTag;
    if (!QuestTag.IsValid())
    {
        UE_LOG(LogSimpleQuest, Warning,
            TEXT("ResolveQuestTag: '%s' is not registered in the runtime tag manager — stale compiled node, skipping. ")
            TEXT("Recompile the owning questline to refresh; if the problem persists, use Stale Quest Tags (Window → Developer Tools → Debug)."),
            *TagName.ToString());
        return;
    }
    UE_LOG(LogSimpleQuest, Verbose, TEXT("ResolveQuestTag: %s → DisplayName='%s'"), *QuestTag.ToString(), *NodeInfo.DisplayName.ToString());
}

const TArray<FName>* UQuestNodeBase::GetNextNodesForPath(FName PathIdentity) const
{
    const FQuestPathNodeList* List = NextNodesByPath.Find(PathIdentity);
    return List ? &List->NodeTags : nullptr;
}
