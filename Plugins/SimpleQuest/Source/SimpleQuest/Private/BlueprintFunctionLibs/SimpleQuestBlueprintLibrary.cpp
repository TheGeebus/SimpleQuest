// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "BlueprintFunctionLibs/SimpleQuestBlueprintLibrary.h"
#include "SimpleQuestLog.h"
#include "Subsystems/WorldStateSubsystem.h"
#include "Subsystems/SignalSubsystem.h"
#include "Utilities/QuestLifecycleQuery.h"
#include "BlueprintAsync/QuestLifecycleObserver.h"
#include "Engine/GameInstance.h"
#include "Events/QuestActivationRequestEvent.h"
#include "Events/QuestBlockRequestEvent.h"
#include "Events/QuestClearBlockRequestEvent.h"
#include "Events/QuestDeactivateRequestEvent.h"
#include "Events/QuestDeactivatedEvent.h"
#include "Events/QuestGivenEvent.h"
#include "Events/QuestlineStartRequestEvent.h"
#include "Events/QuestResolveRequestEvent.h"
#include "Subsystems/QuestManagerSubsystem.h"
#include "Subsystems/QuestStateSubsystem.h"
#include "Objectives/QuestObjective.h"
#include "Quests/QuestlineGraph.h"
#include "Quests/QuestNodeBase.h"
#include "Quests/QuestRewardNode.h"
#include "Quests/Types/QuestOutcomeTags.h"
#include "Rewards/QuestRewardBase.h"


// -------------------------------------------------------------------------
// Private helpers
// -------------------------------------------------------------------------

UWorldStateSubsystem* USimpleQuestBlueprintLibrary::GetWorldStateSubsystem(const UObject* WorldContext)
{
    if (!WorldContext) return nullptr;
    const UWorld* World = WorldContext->GetWorld();
    if (!World) return nullptr;
    UGameInstance* GI = World->GetGameInstance();
    return GI ? GI->GetSubsystem<UWorldStateSubsystem>() : nullptr;
}

USignalSubsystem* USimpleQuestBlueprintLibrary::GetSignalSubsystem(const UObject* WorldContext)
{
    if (!WorldContext) return nullptr;
    const UWorld* World = WorldContext->GetWorld();
    if (!World) return nullptr;
    UGameInstance* GI = World->GetGameInstance();
    return GI ? GI->GetSubsystem<USignalSubsystem>() : nullptr;
}

UQuestManagerSubsystem* USimpleQuestBlueprintLibrary::GetQuestManagerSubsystem(const UObject* WorldContext)
{
    if (!WorldContext) return nullptr;
    const UWorld* World = WorldContext->GetWorld();
    if (!World) return nullptr;
    UGameInstance* GI = World->GetGameInstance();
    return GI ? GI->GetSubsystem<UQuestManagerSubsystem>() : nullptr;
}

UQuestStateSubsystem* USimpleQuestBlueprintLibrary::GetQuestStateSubsystem(const UObject* WorldContext)
{
    if (!WorldContext) return nullptr;
    const UWorld* World = WorldContext->GetWorld();
    if (!World) return nullptr;
    UGameInstance* GI = World->GetGameInstance();
    return GI ? GI->GetSubsystem<UQuestStateSubsystem>() : nullptr;
}

// -------------------------------------------------------------------------
// Quest state queries
// -------------------------------------------------------------------------

bool USimpleQuestBlueprintLibrary::IsQuestLive(const UObject* WorldContext, FGameplayTag QuestTag)
{
    return FQuestLifecycleQuery::IsLive(GetWorldStateSubsystem(WorldContext), QuestTag);
}

bool USimpleQuestBlueprintLibrary::IsQuestCompleted(const UObject* WorldContext, FGameplayTag QuestTag)
{
    return FQuestLifecycleQuery::IsCompleted(GetWorldStateSubsystem(WorldContext), QuestTag);
}

bool USimpleQuestBlueprintLibrary::IsQuestPendingGiver(const UObject* WorldContext, FGameplayTag QuestTag)
{
    return FQuestLifecycleQuery::IsPendingGiver(GetWorldStateSubsystem(WorldContext), QuestTag);
}

bool USimpleQuestBlueprintLibrary::IsQuestAdvancementHeld(const UObject* WorldContext, FGameplayTag QuestTag)
{
    const UQuestManagerSubsystem* Manager = GetQuestManagerSubsystem(WorldContext);
    return Manager ? Manager->IsQuestAdvancementHeld(QuestTag) : false;
}

TArray<FName> USimpleQuestBlueprintLibrary::GetActiveHoldReasons(const UObject* WorldContext, FGameplayTag QuestTag)
{
    const UQuestManagerSubsystem* Manager = GetQuestManagerSubsystem(WorldContext);
    return Manager ? Manager->GetActiveHoldReasons(QuestTag) : TArray<FName>();
}

bool USimpleQuestBlueprintLibrary::IsQuestBlocked(const UObject* WorldContext, FGameplayTag QuestTag)
{
    return FQuestLifecycleQuery::IsBlocked(GetWorldStateSubsystem(WorldContext), QuestTag);
}

bool USimpleQuestBlueprintLibrary::IsQuestResolvedWith(const UObject* WorldContext, FGameplayTag QuestTag, FGameplayTag OutcomeTag)
{
    if (!QuestTag.IsValid() || !OutcomeTag.IsValid()) return false;
    // UQuestStateSubsystem::HasResolvedWith works for any OutcomeTag the quest has actually fired with,
    // including dynamic outcomes set via the BP ResolveQuest helper that were never registered as
    // compile-time identities.
    if (!WorldContext) return false;
    const UWorld* World = WorldContext->GetWorld();
    const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
    const UQuestStateSubsystem* StateSubsystem = GI ? GI->GetSubsystem<UQuestStateSubsystem>() : nullptr;
    return StateSubsystem && StateSubsystem->HasResolvedWith(QuestTag, OutcomeTag);
}

int32 USimpleQuestBlueprintLibrary::GetQuestCompletionCount(const UObject* WorldContext, const FGameplayTag QuestTag)
{
    UQuestManagerSubsystem* QM = GetQuestManagerSubsystem(WorldContext);
    return QM ? QM->GetQuestCompletionCount(QuestTag) : 0;
}

// -------------------------------------------------------------------------
// Quest actions
// -------------------------------------------------------------------------

void USimpleQuestBlueprintLibrary::DeactivateQuest(const UObject* WorldContext, FGameplayTag QuestTag, const FQuestEventPayload& Payload)
{
    if (USignalSubsystem* SS = GetSignalSubsystem(WorldContext))
    {
        SS->PublishMessage(Tag_Channel_QuestDeactivateRequest, FQuestDeactivateRequestEvent(QuestTag, EDeactivationSource::External, Payload));
    }
}

void USimpleQuestBlueprintLibrary::GiveQuest(const UObject* WorldContext, FGameplayTag QuestTag, const FQuestObjectiveActivationContext& Params)
{
    if (USignalSubsystem* SS = GetSignalSubsystem(WorldContext))
    {
        SS->PublishMessage(Tag_Channel_QuestGiven, FQuestGivenEvent(QuestTag, Params));
    }
}

void USimpleQuestBlueprintLibrary::ActivateQuest(const UObject* WorldContext, FGameplayTag QuestTag, const FQuestObjectiveActivationContext& Params, bool bBypassPrerequisites)
{
    if (USignalSubsystem* SS = GetSignalSubsystem(WorldContext))
    {
        SS->PublishMessage(Tag_Channel_QuestActivationRequest, FQuestActivationRequestEvent(QuestTag, Params, bBypassPrerequisites));
    }
}

void USimpleQuestBlueprintLibrary::SetQuestBlocked(const UObject* WorldContext, FGameplayTag QuestTag, const FQuestEventPayload& Payload, bool bAlsoDeactivate)
{
    if (USignalSubsystem* SS = GetSignalSubsystem(WorldContext))
    {
        SS->PublishMessage(Tag_Channel_QuestBlockRequest, FQuestBlockRequestEvent(QuestTag, EDeactivationSource::External, Payload, bAlsoDeactivate));
    }
}

void USimpleQuestBlueprintLibrary::ClearQuestBlocked(const UObject* WorldContext, FGameplayTag QuestTag, const FQuestEventPayload& Payload)
{
    if (USignalSubsystem* SS = GetSignalSubsystem(WorldContext))
    {
        SS->PublishMessage(Tag_Channel_QuestClearBlockRequest, FQuestClearBlockRequestEvent(QuestTag, EDeactivationSource::External, Payload));
    }
}

void USimpleQuestBlueprintLibrary::ResetQuestRunState(const UObject* WorldContext, FGameplayTag QuestTag)
{
    if (UQuestManagerSubsystem* Manager = GetQuestManagerSubsystem(WorldContext))
    {
        Manager->ResetQuestRunState(QuestTag);
    }
}

void USimpleQuestBlueprintLibrary::ResolveQuest(const UObject* WorldContext, FGameplayTag QuestTag, FGameplayTag OutcomeTag, bool bOverrideExisting, const FQuestEventPayload& Payload)
{
    if (USignalSubsystem* SS = GetSignalSubsystem(WorldContext))
    {
        SS->PublishMessage(Tag_Channel_QuestResolveRequest, FQuestResolveRequestEvent(QuestTag, OutcomeTag, bOverrideExisting, Payload));
    }
}

void USimpleQuestBlueprintLibrary::StartQuestline(const UObject* WorldContext, TSoftObjectPtr<UQuestlineGraph> QuestlineGraph, const FQuestObjectiveActivationContext& Params)
{
    if (USignalSubsystem* SS = GetSignalSubsystem(WorldContext))
    {
        SS->PublishMessage(Tag_Channel_QuestlineStartRequest, FQuestlineStartRequestEvent(QuestlineGraph, Params));
    }
}

void USimpleQuestBlueprintLibrary::RestoreQuestline(const UObject* WorldContext, TSoftObjectPtr<UQuestlineGraph> QuestlineGraph)
{
    if (USignalSubsystem* SS = GetSignalSubsystem(WorldContext))
    {
        SS->PublishMessage(Tag_Channel_QuestlineStartRequest, FQuestlineStartRequestEvent(QuestlineGraph, true));
    }
}

void USimpleQuestBlueprintLibrary::ApplyQuestSnapshot(const UObject* WorldContext, const FSimpleQuestSaveSnapshot& Snapshot, bool bRestoreOnNextLevelLoad)
{
    UQuestStateSubsystem* QSS = GetQuestStateSubsystem(WorldContext);
    if (!QSS)
    {
        UE_LOG(LogSimpleQuest, Warning, TEXT("ApplyQuestSnapshot: no QuestStateSubsystem for the given world context; nothing applied."));
        return;
    }

    QSS->ApplySnapshot(Snapshot);   // restore facts + registries (synchronous)

    if (UQuestManagerSubsystem* Manager = GetQuestManagerSubsystem(WorldContext))
    {
        Manager->StashPendingRestore(Snapshot.ActiveGraphs, Snapshot.DeferredActivations, Snapshot.ObjectiveStates);
        if (bRestoreOnNextLevelLoad)
        {
            Manager->ArmRestoreOnNextLevelLoad();
        }
    }
}

void USimpleQuestBlueprintLibrary::RestoreQuestGraphs(const UObject* WorldContext)
{
    if (UQuestManagerSubsystem* Manager = GetQuestManagerSubsystem(WorldContext))
    {
        Manager->RestorePendingGraphs();
    }
    else
    {
        UE_LOG(LogSimpleQuest, Warning, TEXT("RestoreQuestGraphs: no QuestManagerSubsystem for the given world context; nothing restored."));
    }
}

FQuestAdvancementHold USimpleQuestBlueprintLibrary::HoldQuestAdvancement(const UObject* WorldContext, FGameplayTag QuestTag, FName Reason, bool bHoldDeactivation)
{
    if (UQuestManagerSubsystem* Manager = GetQuestManagerSubsystem(WorldContext))
    {
        return Manager->HoldQuestAdvancement(QuestTag, Reason, bHoldDeactivation);
    }
    UE_LOG(LogSimpleQuest, Warning, TEXT("HoldQuestAdvancement: no QuestManagerSubsystem for the given world context; nothing held."));
    return FQuestAdvancementHold();
}

void USimpleQuestBlueprintLibrary::ReleaseQuestAdvancement(const UObject* WorldContext, const FQuestAdvancementHold& Hold)
{
    if (UQuestManagerSubsystem* Manager = GetQuestManagerSubsystem(WorldContext))
    {
        Manager->ReleaseQuestAdvancement(Hold);
    }
}

FSimpleQuestSaveSnapshot USimpleQuestBlueprintLibrary::CaptureQuestState(const UObject* WorldContext)
{
    FSimpleQuestSaveSnapshot Snapshot{};

    UQuestStateSubsystem* QSS = GetQuestStateSubsystem(WorldContext);
    if (!QSS)
    {
        UE_LOG(LogSimpleQuest, Warning, TEXT("CaptureQuestState: no QuestStateSubsystem for the given world context; returning empty snapshot."));
        return Snapshot;
    }

    // Drain advancement holds BEFORE the snapshot is taken. Releasing replays every parked activation, so the save
    // records the successor as Live - an ordinary supported state - instead of a pause that would need its own schema.
    // It is also what keeps Held facts out of saves: by the time CaptureSnapshot gathers facts they are already gone,
    // so no transient-fact mechanism is needed anywhere.
    // THE ORDER IS LOAD-BEARING. Draining after the snapshot would let the released activations change the world after
    // the picture was taken, and they would be lost.
    if (UQuestManagerSubsystem* Manager = GetQuestManagerSubsystem(WorldContext))
    {
        const int32 Drained = Manager->ReleaseAllQuestAdvancementHolds();
        if (Drained > 0)
        {
            UE_LOG(LogSimpleQuest, Log,
                TEXT("CaptureQuestState: released %d advancement hold(s) before capture - pacing does not survive a save."), Drained);
        }
    }

    Snapshot = QSS->CaptureSnapshot();

    // Record which graphs are in play + which nodes are armed-and-waiting on a prereq, so restore is fully self-driving:
    // the save carries both "which graphs to rebuild" and "which deferred activations to re-arm."
    if (UQuestManagerSubsystem* Manager = GetQuestManagerSubsystem(WorldContext))
    {
        Snapshot.ActiveGraphs = Manager->GetKnownLoadedGraphPaths().Array();
        Snapshot.DeferredActivations = Manager->CaptureDeferredActivations();
        Snapshot.ObjectiveStates = Manager->CaptureObjectiveStates();
    }

    return Snapshot;
}

void USimpleQuestBlueprintLibrary::RestoreQuestState(const UObject* WorldContext, const FSimpleQuestSaveSnapshot& Snapshot)
{
    // In-place one-shot: apply the data (+ stash) then immediately rebuild the graphs, all in the current level. For a
    // level-transition load, call ApplyQuestSnapshot BEFORE OpenLevel and RestoreQuestGraphs in the target level instead.
    ApplyQuestSnapshot(WorldContext, Snapshot);
    RestoreQuestGraphs(WorldContext);
}

void USimpleQuestBlueprintLibrary::LogSimpleQuestMessage(const FString& Message, EQuestLogLevel Level)
{
    switch (Level)
    {
    case EQuestLogLevel::Error:   UE_LOG(LogSimpleQuest, Error,   TEXT("%s"), *Message); break;
    case EQuestLogLevel::Display: UE_LOG(LogSimpleQuest, Display, TEXT("%s"), *Message); break;
    case EQuestLogLevel::Verbose: UE_LOG(LogSimpleQuest, Verbose, TEXT("%s"), *Message); break;
    case EQuestLogLevel::Warning:
    default:                      UE_LOG(LogSimpleQuest, Warning, TEXT("%s"), *Message); break;
    }
}


// -------------------------------------------------------------------------
// Observe Quest Lifecycle
// -------------------------------------------------------------------------

UQuestLifecycleObserver* USimpleQuestBlueprintLibrary::ObserveQuestLifecycle(UObject* WorldContextObject, FGameplayTag QuestTag, int32 ExposedEvents, ESignalRoutingMode Routing)
{
    UQuestLifecycleObserver* Sub = NewObject<UQuestLifecycleObserver>();
    Sub->InitFromFactory(WorldContextObject, QuestTag, ExposedEvents, Routing);
    Sub->RegisterWithGameInstance(WorldContextObject);
    return Sub;
}

void USimpleQuestBlueprintLibrary::UnsubscribeFromQuestEvent(UObject* WorldContextObject, const FGameplayTag& QuestTag, FDelegateHandle Handle)
{
    if (!Handle.IsValid()) return;
    if (USignalSubsystem* Signals = GetSignalSubsystem(WorldContextObject))
    {
        Signals->UnsubscribeMessage(QuestTag, Handle);
    }
}

TArray<FQuestRoleSourceInfo> USimpleQuestBlueprintLibrary::GetActiveTriggersForTag(const UObject* WorldContext, FGameplayTag QueryTag)
{
    if (const UQuestStateSubsystem* QSS = GetQuestStateSubsystem(WorldContext))
    {
        return QSS->GetActiveTriggersForTag(QueryTag);
    }
    return {};
}

TArray<FQuestRoleSourceInfo> USimpleQuestBlueprintLibrary::GetActiveGiversForTag(const UObject* WorldContext, FGameplayTag QueryTag)
{
    if (const UQuestStateSubsystem* QSS = GetQuestStateSubsystem(WorldContext))
    {
        return QSS->GetActiveGiversForTag(QueryTag);
    }
    return {};
}

TArray<FQuestRoleSourceInfo> USimpleQuestBlueprintLibrary::GetActiveObserversForTag(const UObject* WorldContext, FGameplayTag QueryTag)
{
    if (const UQuestStateSubsystem* QSS = GetQuestStateSubsystem(WorldContext))
    {
        return QSS->GetActiveObserversForTag(QueryTag);
    }
    return {};
}

UQuestObjective* USimpleQuestBlueprintLibrary::GetActiveObjectiveForTag(const UObject* WorldContext, FGameplayTag QueryTag)
{
    if (const UQuestStateSubsystem* QSS = GetQuestStateSubsystem(WorldContext))
    {
        return QSS->GetActiveObjectiveForTag(QueryTag);
    }
    return nullptr;
}

TArray<FQuestRewardPreview> USimpleQuestBlueprintLibrary::GetAdvertisedRewardsForAnyOutcome(const UObject* WorldContext, FGameplayTag ContentTag, AActor* Viewer)
{
    const UQuestManagerSubsystem* Manager = GetQuestManagerSubsystem(WorldContext);
    // Any-outcome bucket: PathIdentity = NAME_None. bIncludeAnyOutcome is moot when the path IS the any-outcome bucket.
    return Manager ? Manager->ResolveAdvertisedRewards(ContentTag, NAME_None, Viewer, true) : TArray<FQuestRewardPreview>{};
}

TMap<FGameplayTag, FQuestRewardPreviewList> USimpleQuestBlueprintLibrary::GetAdvertisedRewardsFromAsset(const UQuestlineGraph* Questline, FGameplayTag Tag, AActor* Viewer)
{
    TMap<FGameplayTag, FQuestRewardPreviewList> Out;
    if (!Questline) return Out;

    const TMap<FName, TObjectPtr<UQuestNodeBase>>& Nodes = Questline->GetCompiledNodes();
    const UQuestNodeBase* Owner = Nodes.FindRef(Tag.GetTagName());

    // Node channel - the same shared walk the live path uses, sourced from the asset's compiled nodes rather than the
    // manager's live map, so cold and live cannot drift apart.
    if (Owner)
    {
        UGameplayTagsManager& TagManager = UGameplayTagsManager::Get();
        for (const TPair<FName, FQuestReachableRewards>& Pair : Owner->GetReachableRewardsByPath())
        {
            const FGameplayTag OutcomeTag = Pair.Key.IsNone()
                ? TAG_Outcome_AnyOutcome.GetTag()
                : TagManager.RequestGameplayTag(Pair.Key, false);
            if (!OutcomeTag.IsValid()) continue;   // dynamic PathName - no author-time outcome tag to key on

            TArray<FQuestRewardPreview> Previews = UQuestRewardNode::ResolveAdvertisedFromManifest(
                Owner->GetReachableRewardsByPath(), Nodes, Pair.Key, Viewer, false, Tag);
            if (Previews.Num() > 0) Out.FindOrAdd(OutcomeTag).Previews.Append(MoveTemp(Previews));
        }
    }

    // Questline channel - cold has no manager, so the identity resolution the live path delegates to
    // ResolveQuestlineRewards happens here against the asset's own compiled map.
    FGameplayTag IdentityTag = Tag;
    if (Owner && Owner->GetLinkedInnerIdentityTag().IsValid())
    {
        IdentityTag = Owner->GetLinkedInnerIdentityTag();
    }

    if (const FQuestCompiledQuestlineRewards* Compiled = Questline->GetCompiledQuestlineRewards().Find(IdentityTag.GetTagName()))
    {
        for (const TPair<FGameplayTag, FQuestRewardSet>& Pair : Compiled->RewardsByOutcome)
        {
            TArray<FQuestRewardPreview> Previews;
            for (const TObjectPtr<UQuestRewardBase>& Reward : Pair.Value.Rewards)
            {
                if (Reward) Previews.Append(Reward->DispatchDescribeReward(Viewer, IdentityTag));
            }
            if (Previews.Num() > 0) Out.FindOrAdd(Pair.Key).Previews.Append(MoveTemp(Previews));
        }
    }

    return Out;
}

TMap<FGameplayTag, FQuestRewardPreviewList> USimpleQuestBlueprintLibrary::GetQuestlineRewardsFromAsset(const UQuestlineGraph* Questline, AActor* Viewer)
{
    TMap<FGameplayTag, FQuestRewardPreviewList> Out;
    if (!Questline) return Out;

    // Read the authored questline-level rewards directly off the asset (this map IS the runtime home) - manager-free,
    // works cold. Describe each reward for the viewer; group by the outcome that pays it.
    for (const TPair<FGameplayTag, FQuestRewardSet>& Pair : Questline->GetQuestlineRewards())
    {
        FQuestRewardPreviewList List;
        for (const TObjectPtr<UQuestRewardBase>& Reward : Pair.Value.Rewards)
        {
            if (Reward) List.Previews.Append(Reward->DispatchDescribeReward(Viewer, Questline->GetIdentityTag()));
        }
        if (List.Previews.Num() > 0) Out.Add(Pair.Key, MoveTemp(List));
    }
    return Out;
}

TMap<FGameplayTag, FQuestRewardPreviewList> USimpleQuestBlueprintLibrary::GetQuestlineRewards(const UObject* WorldContext, FGameplayTag QuestlineTag, AActor* Viewer)
{
    const UQuestManagerSubsystem* Manager = GetQuestManagerSubsystem(WorldContext);
    return Manager ? Manager->ResolveQuestlineRewards(QuestlineTag, Viewer) : TMap<FGameplayTag, FQuestRewardPreviewList>{};
}

TMap<FGameplayTag, FQuestRewardPreviewList> USimpleQuestBlueprintLibrary::GetAdvertisedRewards(const UObject* WorldContext, FGameplayTag Tag, AActor* Viewer)
{
    const UQuestManagerSubsystem* Manager = GetQuestManagerSubsystem(WorldContext);
    return Manager ? Manager->ResolveAllRewards(Tag, Viewer) : TMap<FGameplayTag, FQuestRewardPreviewList>{};
}

TArray<FQuestRewardPreview> USimpleQuestBlueprintLibrary::GetAdvertisedRewardsForOutcome(const UObject* WorldContext, FGameplayTag ContentTag, FGameplayTag OutcomeTag, AActor* Viewer, bool bIncludeAnyOutcome)
{
    const UQuestManagerSubsystem* Manager = GetQuestManagerSubsystem(WorldContext);
    if (!Manager) return {};

    // Filtered off the MERGED map rather than the node manifest alone, so asking about one outcome folds in
    // questline-level rewards exactly as the whole-map query does. Any-Outcome is a separate key there, so the union
    // that bIncludeAnyOutcome asks for happens here rather than inside the walk.
    const TMap<FGameplayTag, FQuestRewardPreviewList> All = Manager->ResolveAllRewards(ContentTag, Viewer);

    TArray<FQuestRewardPreview> Out;
    if (const FQuestRewardPreviewList* Named = All.Find(OutcomeTag)) Out.Append(Named->Previews);

    // *** ASKING FOR ANY-OUTCOME ALREADY RETURNED THE ANY-OUTCOME LIST. *** The flag means "also include the rewards
    // that fire regardless of outcome" - when that IS the outcome asked about, there is nothing further to add, and
    // unioning it would advertise every any-outcome reward twice. Idempotent rather than additive.
    if (bIncludeAnyOutcome && OutcomeTag != TAG_Outcome_AnyOutcome.GetTag())
    {
        if (const FQuestRewardPreviewList* Any = All.Find(TAG_Outcome_AnyOutcome.GetTag())) Out.Append(Any->Previews);
    }
    return Out;
}

TMap<FGameplayTag, FQuestRewardPreviewList> USimpleQuestBlueprintLibrary::GetAllAdvertisedRewardsByOutcome(const UObject* WorldContext, FGameplayTag ContentTag, AActor* Viewer)
{
    const UQuestManagerSubsystem* Manager = GetQuestManagerSubsystem(WorldContext);
    return Manager ? Manager->ResolveAllAdvertisedRewardsByOutcome(ContentTag, Viewer) : TMap<FGameplayTag, FQuestRewardPreviewList>{};
}

FText USimpleQuestBlueprintLibrary::GetQuestDisplayNameText(const UObject* WorldContext, const FGameplayTag QueryTag)
{
    if (const UQuestStateSubsystem* QSS = GetQuestStateSubsystem(WorldContext))
    {
        return QSS->GetDisplayName(QueryTag);
    }
    return {};
}

FText USimpleQuestBlueprintLibrary::GetQuestDescriptionText(const UObject* WorldContext, const FGameplayTag QueryTag)
{
    if (const UQuestStateSubsystem* QSS = GetQuestStateSubsystem(WorldContext))
    {
        return QSS->GetDisplayDescription(QueryTag);
    }
    return {};
}

UQuestDisplayData* USimpleQuestBlueprintLibrary::GetQuestDisplayDataAsset(const UObject* WorldContext, const FGameplayTag QueryTag)
{
    if (const UQuestStateSubsystem* QSS = GetQuestStateSubsystem(WorldContext))
    {
        return QSS->GetDisplayData(QueryTag);
    }
    return nullptr;
}

