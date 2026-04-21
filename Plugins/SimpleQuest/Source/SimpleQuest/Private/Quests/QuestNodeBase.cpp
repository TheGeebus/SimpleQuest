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
    OnNodeActivated.ExecuteIfBound(this, InContextualTag);
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
    QuestTag = UGameplayTagsManager::Get().RequestGameplayTag(TagName);
    NodeInfo.QuestTag = QuestTag;
    UE_LOG(LogSimpleQuest, Verbose, TEXT("ResolveQuestTag: %s → DisplayName='%s'"), *QuestTag.ToString(), *NodeInfo.DisplayName.ToString());
}

const TArray<FName>* UQuestNodeBase::GetNextNodesForOutcome(FGameplayTag OutcomeTag) const
{
    const FQuestOutcomeNodeList* List = NextNodesByOutcome.Find(OutcomeTag);
    return List ? &List->NodeTags : nullptr;
}
