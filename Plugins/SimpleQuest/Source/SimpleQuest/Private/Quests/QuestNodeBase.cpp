// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Quests/QuestNodeBase.h"
#include "GameplayTagsManager.h"
#include "SimpleQuestLog.h"
#include "Signals/SignalSubsystem.h"
#include "WorldState/WorldStateSubsystem.h"
#include "Subsystems/QuestStateSubsystem.h"
#include "Events/QuestResolutionRecordedEvent.h"


void UQuestNodeBase::Activate(FGameplayTag InContextualTag)
{
    UWorldStateSubsystem* WorldState = CachedGameInstance.IsValid() ? CachedGameInstance->GetSubsystem<UWorldStateSubsystem>() : nullptr;
    UQuestStateSubsystem* StateSubsystem = CachedGameInstance.IsValid() ? CachedGameInstance->GetSubsystem<UQuestStateSubsystem>() : nullptr;
    if (PrerequisiteExpression.IsAlways() || PrerequisiteExpression.Evaluate(WorldState, StateSubsystem))
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

    // Subscribe per-leaf with type-aware channel: WorldState fact channel for Leaf, resolution-recorded channel
    // for Leaf_Resolution. Either path triggers TryActivateDeferred via its handler. Mirrors the same shape used
    // in UQuestManagerSubsystem::RegisterEnablementWatch / DeferChainToNextNodes and UQuestPrereqRuleNode::Activate
    // - fourth site in the prereq-subscription duplication pattern flagged for the centralized leaf-construction
    // refactor parked under FUTURE EXTENSIBILITY CANDIDATES.
    TArray<FPrereqLeafDescriptor> Leaves;
    PrerequisiteExpression.CollectLeaves(Leaves);

    for (const FPrereqLeafDescriptor& Leaf : Leaves)
    {
        if (Leaf.Type == EPrerequisiteExpressionType::Leaf)
        {
            const FGameplayTag& LeafTag = Leaf.FactTag;
            if (!LeafTag.IsValid() || PrereqSubscriptionHandles.Contains(LeafTag)) continue;
            FDelegateHandle Handle = Signals->SubscribeMessage<FWorldStateFactAddedEvent>(LeafTag, this, &UQuestNodeBase::OnPrereqFactAdded);
            PrereqSubscriptionHandles.Add(LeafTag, Handle);
        }
        else if (Leaf.Type == EPrerequisiteExpressionType::Leaf_Resolution)
        {
            const FGameplayTag& LeafQuestTag = Leaf.ResolutionQuestTag;
            if (!LeafQuestTag.IsValid() || PrereqSubscriptionHandles.Contains(LeafQuestTag)) continue;
            FDelegateHandle Handle = Signals->SubscribeMessage<FQuestResolutionRecordedEvent>(LeafQuestTag, this, &UQuestNodeBase::OnPrereqResolutionRecorded);
            PrereqSubscriptionHandles.Add(LeafQuestTag, Handle);
        }
    }
}

void UQuestNodeBase::OnPrereqFactAdded(FGameplayTag Channel, const FWorldStateFactAddedEvent& Event)
{
    TryActivateDeferred();
}

void UQuestNodeBase::OnPrereqResolutionRecorded(FGameplayTag Channel, const FQuestResolutionRecordedEvent& Event)
{
    TryActivateDeferred();
}

void UQuestNodeBase::TryActivateDeferred()
{
    if (!CachedGameInstance.IsValid()) return;
    UWorldStateSubsystem* WorldState = CachedGameInstance->GetSubsystem<UWorldStateSubsystem>();
    UQuestStateSubsystem* StateSubsystem = CachedGameInstance->GetSubsystem<UQuestStateSubsystem>();
    if (!WorldState || !StateSubsystem) return;

    if (!PrerequisiteExpression.Evaluate(WorldState, StateSubsystem)) return;

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
