// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Subsystems/QuestManagerSubsystem.h"
#include "GameplayTagsManager.h"
#include "SimpleQuestLog.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Display/QuestDisplayData.h"
#include "Events/QuestEndedEvent.h"
#include "Events/QuestTriggerFiredEvent.h"
#include "Events/QuestProgressEvent.h"
#include "Events/QuestStartedEvent.h"
#include "Events/QuestEnabledEvent.h"
#include "Events/QuestDeactivatedEvent.h"
#include "Events/QuestActivatedEvent.h"
#include "Events/QuestActivationRequestEvent.h"
#include "Events/QuestActivationFailedEvent.h"
#include "Events/QuestBlockRequestEvent.h"
#include "Events/QuestClearBlockRequestEvent.h"
#include "Events/QuestDeactivateRequestEvent.h"
#include "Events/QuestDisabledEvent.h"
#include "Events/QuestGiveBlockedEvent.h"
#include "Events/QuestGivenEvent.h"
#include "Events/QuestGiverRegisteredEvent.h"
#include "Events/QuestlineStartRequestEvent.h"
#include "Events/QuestResolveRequestEvent.h"
#include "Events/QuestResolutionRecordedEvent.h"
#include "Events/QuestEntryRecordedEvent.h"
#include "Events/QuestBlockedEvent.h"
#include "Events/QuestUnblockedEvent.h"
#include "Events/QuestProgressRefusedEvent.h"
#include "Events/QuestTriggerDeactivatedEvent.h"
#include "Events/QuestTriggerResponseEvent.h"
#include "Events/QuestTriggerSatisfiedEvent.h"
#include "Objectives/QuestObjective.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Quests/Quest.h"
#include "Quests/QuestlineGraph.h"
#include "Quests/QuestNodeBase.h"
#include "Quests/QuestRewardNode.h"
#include "Quests/QuestStep.h"
#include "Quests/Types/PrereqLeafSubscription.h"
#include "Quests/Types/QuestEventPayload.h"
#include "Quests/Types/QuestObjectiveTriggerContext.h"
#include "Quests/Types/QuestOutcomeTags.h"
#include "Settings/SimpleQuestSettings.h"
#include "StructUtils/InstancedStruct.h"
#include "Subsystems/SignalSubsystem.h"
#include "Subsystems/QuestStateSubsystem.h"
#include "Subsystems/WorldStateSubsystem.h"
#include "Utilities/QuestTagComposer.h"
#include "Utilities/QuestActivationGuard.h"
#include "Utilities/QuestLifecycleQuery.h"
#include "Utilities/QuestPublish.h"
#include "Misc/ScopeExit.h"
#include "Quests/Types/QuestRewardActivationContext.h"
#include "Rewards/QuestRewardBase.h"
#if WITH_EDITOR
#include "Components/QuestGiverComponent.h"
#endif

namespace
{
    /**
     * Overlay a caller-supplied attribution payload onto a framework-assembled context. Returns Base with
     * Overlay's populated FQuestContextBase fields layered on top - NodeInfo (graph identity) stays from
     * Base; Instigator / CustomData / OriginTag / OriginChain from Overlay win where populated. Used at
     * request-handler boundaries to let BP callers' attribution data carry through into lifecycle event
     * payloads without losing the framework's node-perspective metadata.
     */
    FQuestEventPayload OverlayCallerContext(const FQuestEventPayload& Base, const FQuestEventPayload& Overlay)
    {
        FQuestEventPayload Result = Base;
        if (Overlay.Instigator.IsValid())   Result.Instigator = Overlay.Instigator;
        if (Overlay.CustomData.IsValid())   Result.CustomData = Overlay.CustomData;
        if (Overlay.OriginTag.IsValid())    Result.OriginTag = Overlay.OriginTag;
        if (!Overlay.OriginChain.IsEmpty()) Result.OriginChain = Overlay.OriginChain;
        return Result;
    }

    /**
     * Resolve the publish anchor for a Reason=UnknownQuest activation failure. Returns the input tag itself when
     * registered (state-vs-cache desync - tag known, no loaded instance). Otherwise walks up the input FName by
     * string parsing to find the first registered ancestor (covers stale tags whose FName is preserved across BP
     * serialization but whose registry entry has been removed). Returns invalid if no registered ancestor exists
     * in the input's name lineage.
     *
     * Without the walk-up, publishes on loose tags fire the bus trace but reach no subscribers - RequestDirect-
     * Parent returns invalid for unregistered tags, so the bus's per-publish parent walk exits before climbing
     * to any registered ancestor.
     */
    FGameplayTag ResolveActivationFailurePublishAnchor(FName InputName)
    {
        UGameplayTagsManager& TagManager = UGameplayTagsManager::Get();
        FGameplayTag Anchor = TagManager.RequestGameplayTag(InputName, false);
        if (Anchor.IsValid()) return Anchor;

        FString NameStr = InputName.ToString();
        while (true)
        {
            int32 LastDot;
            if (!NameStr.FindLastChar(TEXT('.'), LastDot)) break;
            NameStr.LeftInline(LastDot);
            FGameplayTag Candidate = TagManager.RequestGameplayTag(FName(*NameStr), false);
            if (Candidate.IsValid()) return Candidate;
        }
        return FGameplayTag();
    }

    /**
     * Publish a Reason=UnknownQuest FQuestActivationFailedEvent on the resolved publish anchor for the given
     * input FName. When the anchor is a loaded UQuestNodeBase, fans out across its alias perspectives so
     * subscribers bound to any perspective receive. When the anchor is a non-node tag (e.g. a Questline category),
     * single-publish on that tag - the bus's parent walk handles ancestor subscribers.
     *
     * Event.QuestTag is set to the input tag when registered (state-vs-cache desync) and strict-invalid when the
     * input is unregistered (stale-tag activation - the FName lives only in the caller's Warning log per the
     * one-state UnknownQuest contract).
     */
    void PublishUnknownQuestFailure(USignalSubsystem* Signals,
        const TMap<FName, TObjectPtr<UQuestNodeBase>>& LoadedNodeInstances,
        FName InputName, const FQuestEventPayload& Payload)
    {
        if (!Signals) return;

        const FGameplayTag PublishAnchor = ResolveActivationFailurePublishAnchor(InputName);
        if (!PublishAnchor.IsValid()) return;  // No registered ancestor; warning log alone surfaces the failure.

        // InputTag is strict-invalid for unregistered inputs (RequestGameplayTag returns EmptyTag), valid for the
        // state-vs-cache desync case. Either way it's the right value for Event.QuestTag's one-state contract.
        const FGameplayTag InputTag = UGameplayTagsManager::Get().RequestGameplayTag(InputName, false);

        TArray<FGameplayTag> Channels;
        Channels.Add(PublishAnchor);
        if (const TObjectPtr<UQuestNodeBase>* AnchorPtr = LoadedNodeInstances.Find(PublishAnchor.GetTagName()))
        {
            if (UQuestNodeBase* AnchorInstance = *AnchorPtr)
            {
                for (const FGameplayTag& Alias : AnchorInstance->GetAssetScopedAliasTags())
                {
                    if (Alias.IsValid()) Channels.Add(Alias);
                }
            }
        }

        Signals->PublishMessageOnChannels(MoveTemp(Channels), FQuestActivationFailedEvent(InputTag, InputName, EQuestActivationBlocker::UnknownQuest, Payload));
    }
}

void UQuestManagerSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    // Force-init dependencies before any GetSubsystem<T> lookup further down. UE's subsystem collection
    // initializes registered subsystems in arbitrary order; without this, downstream GetSubsystem calls can
    // return null (or a partially-initialized instance) when our Initialize fires before theirs in the
    // collection's iteration order. With these calls, the dependencies are guaranteed fully initialized
    // before we cache their pointers or call into them.
    Collection.InitializeDependency<UWorldStateSubsystem>();
    Collection.InitializeDependency<UQuestStateSubsystem>();
    Collection.InitializeDependency<USignalSubsystem>();
    
    if (UGameInstance* GameInstance = GetGameInstance())
    {
        WorldState = GameInstance->GetSubsystem<UWorldStateSubsystem>();
        QuestStateSubsystem = GameInstance->GetSubsystem<UQuestStateSubsystem>();
        QuestSignalSubsystem = GameInstance->GetSubsystem<USignalSubsystem>();
        if (QuestSignalSubsystem)
        {
            GivenDelegateHandle = QuestSignalSubsystem->SubscribeMessage<FQuestGivenEvent>(Tag_Channel_QuestGiven, this, &UQuestManagerSubsystem::HandleGiveQuestEvent);
            GiverRegisteredDelegateHandle = QuestSignalSubsystem->SubscribeMessage<FQuestGiverRegisteredEvent>(Tag_Channel_QuestGiverRegistered, this, &UQuestManagerSubsystem::HandleGiverRegisteredEvent);
            DeactivateEventDelegateHandle = QuestSignalSubsystem->SubscribeMessage<FQuestDeactivateRequestEvent>(Tag_Channel_QuestDeactivateRequest, this, &UQuestManagerSubsystem::HandleNodeDeactivationRequest);
            ActivationRequestDelegateHandle = QuestSignalSubsystem->SubscribeMessage<FQuestActivationRequestEvent>(Tag_Channel_QuestActivationRequest, this, &UQuestManagerSubsystem::HandleActivationRequest);
            BlockRequestDelegateHandle = QuestSignalSubsystem->SubscribeMessage<FQuestBlockRequestEvent>(Tag_Channel_QuestBlockRequest, this, &UQuestManagerSubsystem::HandleBlockRequest);
            ClearBlockRequestDelegateHandle = QuestSignalSubsystem->SubscribeMessage<FQuestClearBlockRequestEvent>(Tag_Channel_QuestClearBlockRequest, this, &UQuestManagerSubsystem::HandleClearBlockRequest);
            ResolveRequestDelegateHandle = QuestSignalSubsystem->SubscribeMessage<FQuestResolveRequestEvent>(Tag_Channel_QuestResolveRequest, this, &UQuestManagerSubsystem::HandleResolveRequest);
            QuestlineStartRequestDelegateHandle = QuestSignalSubsystem->SubscribeMessage<FQuestlineStartRequestEvent>(Tag_Channel_QuestlineStartRequest, this, &UQuestManagerSubsystem::HandleQuestlineStartRequest);
            
            // IdentityNamespace carries a trailing dot for concatenation ("SimpleQuest.Questline."), which is not a
            // valid tag. Trim it to get the root every node tag descends from.
            const FString RefusalRoot = FQuestTagComposer::IdentityNamespace.LeftChop(1);
            ProgressRefusedRecordHandle = QuestSignalSubsystem->SubscribeMessage<FQuestProgressRefusedEvent>(FGameplayTag::RequestGameplayTag(FName(*RefusalRoot), false), this, &UQuestManagerSubsystem::HandleProgressRefusedForRecord);
            GiveBlockedRecordHandle = QuestSignalSubsystem->SubscribeMessage<FQuestGiveBlockedEvent>(FGameplayTag::RequestGameplayTag(FName(*RefusalRoot), false), this, &UQuestManagerSubsystem::HandleGiveBlockedForRecord);
        }
    }

    RegisterGiversFromAssetRegistry();
    
    // Display index from the compiled ini - file read + small asset loads, no AR wait, so display names/DisplayData
    // resolve for cold BeginPlay queries (nameplates, sidebars) before anything activates.
    LoadCompiledDisplayIni();
    
    UE_LOG(LogSimpleQuestActivation, Log, TEXT("UQuestManagerSubsystem::Initialize : Initializing: %s"), *GetFullName());

    // Build the GroupTag → listener-graphs inverted index from AR metadata. Async-loading itself is deferred to
    // reachability walks during RegisterQuestlineGraph - no graph gets loaded at this point unless something else
    // (e.g. BP_QuestPlayerExample::BeginPlay → StartQuestline) explicitly activates it, which then cascades through
    // WarmReachableGraphs as its outward setters identify further reachable listener graphs.
    IAssetRegistry& AR = FAssetRegistryModule::GetRegistry();
    if (AR.IsLoadingAssets())
    {
        AR.OnFilesLoaded().AddUObject(this, &UQuestManagerSubsystem::BuildListenerGroupIndex);
    }
    else
    {
        BuildListenerGroupIndex();
    }
}

bool UQuestManagerSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    if (!Super::ShouldCreateSubsystem(Outer)) return false;

    // Only the class designated in project settings gets instantiated. Every other concrete UQuestManagerSubsystem
    // subclass discovered by UE is skipped here. InitializeDependency in the initializer force-loads the designated
    // class; this gate suppresses the rest.
    const USimpleQuestSettings* Settings = GetDefault<USimpleQuestSettings>();
    UClass* DesignatedClass = Settings ? Settings->QuestManagerClass.LoadSynchronous() : nullptr;

    // No designated class in settings - fall back to the base class only, so the system still functions out of the box.
    if (!DesignatedClass) return GetClass() == UQuestManagerSubsystem::StaticClass();

    return GetClass() == DesignatedClass;
}

void UQuestManagerSubsystem::Deinitialize()
{
    if (QuestSignalSubsystem)
    {
        QuestSignalSubsystem->UnsubscribeMessage(Tag_Channel_QuestGiven, GivenDelegateHandle);
        QuestSignalSubsystem->UnsubscribeMessage(Tag_Channel_QuestGiverRegistered, GiverRegisteredDelegateHandle);
        QuestSignalSubsystem->UnsubscribeMessage(Tag_Channel_QuestDeactivateRequest, DeactivateEventDelegateHandle);
        QuestSignalSubsystem->UnsubscribeMessage(Tag_Channel_QuestTrigger, ClassBridgeHandle);
        QuestSignalSubsystem->UnsubscribeMessage(Tag_Channel_QuestActivationRequest, ActivationRequestDelegateHandle);
        QuestSignalSubsystem->UnsubscribeMessage(Tag_Channel_QuestBlockRequest, BlockRequestDelegateHandle);
        QuestSignalSubsystem->UnsubscribeMessage(Tag_Channel_QuestClearBlockRequest, ClearBlockRequestDelegateHandle);
        QuestSignalSubsystem->UnsubscribeMessage(Tag_Channel_QuestResolveRequest, ResolveRequestDelegateHandle);
        QuestSignalSubsystem->UnsubscribeMessage(Tag_Channel_QuestlineStartRequest, QuestlineStartRequestDelegateHandle);

        for (auto& Pair : DeactivationSubscriptionHandles)
        {
            QuestSignalSubsystem->UnsubscribeMessage(Pair.Key, Pair.Value);
        }
        DeactivationSubscriptionHandles.Reset();

        for (auto& Pair : DeferredCompletionPrereqHandles)
        {
            FPrereqLeafSubscription::UnsubscribeAll(QuestSignalSubsystem, Pair.Value);
        }
        DeferredCompletionPrereqHandles.Reset();

        for (auto& Pair : EnablementWatchHandles)
        {
            FPrereqLeafSubscription::UnsubscribeAll(QuestSignalSubsystem, Pair.Value);
        }
        EnablementWatchHandles.Reset();
        EnablementWatches.Reset();
        RecentGiverActors.Reset();
    }
    DisarmRestoreOnNextLevelLoad();
    Super::Deinitialize();
}

void UQuestManagerSubsystem::CheckQuestObjectives(FGameplayTag Channel, const FInstancedStruct& RawEvent)
{
    const FQuestTriggerFiredEvent* Event = RawEvent.GetPtr<FQuestTriggerFiredEvent>();
    if (!Event) return;

    TObjectPtr<UQuestNodeBase>* NodePtr = LoadedNodeInstances.Find(Channel.GetTagName());
    if (!NodePtr) return;

    UQuestStep* Step = Cast<UQuestStep>(*NodePtr);
    if (!Step || !Step->GetLiveObjective()) return;

    // Build the trigger context - reused by both refusal-feedback publishes below and the completion dispatch.
    FQuestObjectiveTriggerContext Context;
    Context.TriggeredActor = Cast<AActor>(Event->TriggeredActor);
    Context.Instigator = Cast<AActor>(Event->Instigator);
    Context.CustomData = Event->CustomData;
    Context.CustomTag = Event->CustomTag;
    Context.OriginatingTriggerComponent = Event->OriginatingTriggerComponent;

    // Manual Block gate - a Blocked step refuses trigger-driven progress. Block leaves the trigger active so the player
    // can poke it for feedback (Block doesn't disable targets - that's Deactivate's job), but it can't advance until
    // ClearBlocked. Distinct from the prerequisite gate below: this is the SetBlocked / SetQuestBlocked fact, not a
    // prereq expression. Publishes ProgressRefused with Reason=Blocked so consumers can tell the two refusals apart.
    if (FQuestLifecycleQuery::IsBlocked(WorldState, Step->GetContextualTag()))
    {
        FQuestActivationBlocker Blocker;
        Blocker.Reason = EQuestActivationBlocker::Blocked;
        FQuestPublish::OnAllNodeTags(QuestSignalSubsystem, Step, FQuestProgressRefusedEvent(Step->GetContextualTag(), { Blocker }, Context));
        return;
    }

    if (Step->GetPrerequisiteGateMode() == EPrerequisiteGateMode::GatesProgression
        && !Step->IsGiverGated()
        && !Step->PrerequisiteExpression.IsAlways())
    {
        // Leaf status is still built for the refusal payload - subscribers report WHICH leaves are unsatisfied. The
        // gate decision comes from the hold-aware evaluation instead, so a held source keeps the step gated even
        // though every leaf now reads as satisfied.
        const FQuestPrereqStatus PrereqStatus = Step->PrerequisiteExpression.EvaluateWithLeafStatus(WorldState, QuestStateSubsystem);
        const auto IsSourceHeld = [this](FGameplayTag Source) { return IsQuestAdvancementHeld(Source); };
        const EPrereqTriState GateState = Step->PrerequisiteExpression.EvaluateWithHolds(WorldState, QuestStateSubsystem, IsSourceHeld);
        if (GateState != EPrereqTriState::Satisfied)
        {
            // Trigger-side per-fire feedback for the GatesProgression case. Multichannel publish via
            // FQuestPublish::OnAllNodeTags so subscribers on any perspective receive the refusal.
            //
            // THE REASON DISTINGUISHES TWO SITUATIONS THAT USED TO LOOK IDENTICAL and want opposite responses from a
            // player: Unsatisfied means "go do the thing this depends on", Indeterminate means "everything is done,
            // wait a moment." Reporting the second as PrereqUnmet also shipped an EMPTY UnsatisfiedLeafTags, because
            // every leaf really is satisfied - so a UI listing what remains would have shown nothing at all.
            FQuestActivationBlocker Blocker;
            Blocker.Reason = (GateState == EPrereqTriState::Indeterminate)
                ? EQuestActivationBlocker::HeldForAdvancement
                : EQuestActivationBlocker::PrereqUnmet;

            for (const FQuestPrereqLeafStatus& Leaf : PrereqStatus.Leaves)
            {
                if (!Leaf.bSatisfied) Blocker.UnsatisfiedLeafTags.Add(Leaf.LeafTag);
            }

            FQuestPublish::OnAllNodeTags(QuestSignalSubsystem, Step, FQuestProgressRefusedEvent(Step->GetContextualTag(), { Blocker }, Context));
            return;
        }
    }

    Step->GetLiveObjective()->DispatchTryCompleteObjective(Context);
}

void UQuestManagerSubsystem::RegisterQuestlineGraph(UQuestlineGraph* Graph)
{
    if (!Graph) return;

    // Register this graph under its identity tag so questline-level reward delivery (PublishGraphResolutions) can resolve
    // a resolution's GraphTag back to the asset and read its QuestlineRewards. Identity = the same composition used by
    // ActivateQuestlineGraph's idempotency gate.
    const FString IdentityString = FQuestTagComposer::IdentityNamespace + Graph->GetQuestlineID();
    if (const FGameplayTag IdentityTag = FGameplayTag::RequestGameplayTag(FName(*IdentityString), false); IdentityTag.IsValid())
    {
        LiveGraphsByIdentity.Add(IdentityTag, Graph);
    }

    // Flatten this graph's compiled questline-level rewards (its own + any linked questlines inlined into it) into the
    // by-identity lookup, so delivery and reward queries resolve an embedded questline's rewards without its source
    // asset (which is never loaded at runtime). Keyed by the same identity tag name the compiler attributes resolutions to.
    for (const TPair<FName, FQuestCompiledQuestlineRewards>& Entry : Graph->CompiledQuestlineRewards)
    {
        LiveQuestlineRewardsByIdentity.Add(Entry.Key, Entry.Value);
        UE_LOG(LogSimpleQuestActivation, Verbose, TEXT("RegisterQuestlineGraph: '%s' - compiled questline rewards for identity '%s' (%d outcome set(s))"),
            *Graph->GetName(),
            *Entry.Key.ToString(),
            Entry.Value.RewardsByOutcome.Num());
    }

    // Build a contextual-FName to alias-FNames lookup from the graph's persisted alias pairs, so each instance can
    // resolve its own AssetScopedAliasTags at registration (the class-channel perspectives it publishes on). This
    // makes explicit the alias resolution the old AuthoredNodeGuid merge performed as a side effect - each placement
    // now resolves its aliases independently, with no dependence on a duplicate registration to merge against.
    TMap<FName, TArray<FName>> AliasFNamesByContextual;
    for (const FQuestCompiledNodeAlias& Alias : Graph->GetCompiledNodeAliases())
    {
        AliasFNamesByContextual.FindOrAdd(Alias.ContextualFName).Add(Alias.AliasFName);
    }

    int32 NewlyRegistered = 0;
    int32 SkippedAlreadyRegistered = 0;
    for (const auto& Pair : Graph->GetCompiledNodes())
    {
        if (UQuestNodeBase* Instance = Pair.Value)
        {
            // PrereqRule monitors are singleton-per-rule by design - the same RuleTagName key is emitted by every
            // compile context that references the rule (each emits its own Monitor instance with its own
            // Expression compiled against that context's leaves). This deduplication keeps the first-registered
            // Monitor and skips the rest so the rule has exactly one wired-up evaluator + subscription set in
            // LoadedNodeInstances. The skipped Monitor instances stay dormant in their owning graph's
            // CompiledNodes but never get OnRegisteredWithManager called.
            if (LoadedNodeInstances.Contains(Pair.Key))
            {
                ++SkippedAlreadyRegistered;
                continue;
            }

            // Per-placement registration: every compiled placement is its own runtime instance, keyed in
            // LoadedNodeInstances by its ContextualTag alone. The same authored sub-questline placed N times (or
            // compiled standalone and inlined elsewhere) yields N independent instances with independent progress -
            // they do not merge. Cross-asset observers reach all placements of a class through the shared
            // AssetScopedAliasTag at publish time (see GetAssetScopedAliasTags and the multi-channel publish), not
            // through merged LoadedNodeInstances keys. Util_ keys remain per-context by design.
            const bool bIsUtilityKey = Pair.Key.ToString().StartsWith(TEXT("Util_"));

            // Compiled node instances live on the UQuestlineGraph asset and persist across PIE sessions. Wipe any
            // state the prior session left on them - subscription handles to a dead SignalSubsystem, deferred
            // contextual tags, activation scratch, completion snapshots - so this session starts clean.
            Instance->ResetTransientState();

            if (!bIsUtilityKey)
            {
                Instance->ResolveContextualTag(Pair.Key);
                if (const TArray<FName>* Aliases = AliasFNamesByContextual.Find(Pair.Key))
                {
                    Instance->ResolveAssetScopedAliasTags(*Aliases);
                }
            }
            RegisterLoadedNodeInstance(Pair.Key, Instance);
            Instance->RegisterWithGameInstance(GetGameInstance());
            Instance->OnRegisteredWithManager();
            Instance->OnNodeCompleted.BindDynamic(this, &UQuestManagerSubsystem::HandleOnNodeCompleted);
            Instance->OnNodeStarted.BindDynamic(this, &UQuestManagerSubsystem::HandleOnNodeStarted);
            Instance->OnNodeActivationRefused.BindDynamic(this, &UQuestManagerSubsystem::HandleOnNodeActivationRefused);
            Instance->OnNodeForwardActivated.BindDynamic(this, &UQuestManagerSubsystem::HandleOnNodeForwardActivated);
            const FGameplayTag ResolvedTag = Instance->GetContextualTag();
            if (ResolvedTag.IsValid() && QuestSignalSubsystem)
            {
                FDelegateHandle Handle = QuestSignalSubsystem->SubscribeMessage<FQuestDeactivatedEvent>(ResolvedTag, this, &UQuestManagerSubsystem::HandleNodeDeactivatedEvent);
                DeactivationSubscriptionHandles.Add(ResolvedTag, Handle);
            }
            if (UQuestStep* Step = Cast<UQuestStep>(Instance))
            {
                Step->OnNodeProgress.BindDynamic(this, &UQuestManagerSubsystem::HandleOnNodeProgress);
                Step->OnNodeRefused.BindDynamic(this, &UQuestManagerSubsystem::HandleOnNodeRefused);
                Step->OnNodeTriggerDeactivation.BindDynamic(this, &UQuestManagerSubsystem::HandleOnNodeTriggerDeactivation);
                Step->OnNodeTriggerSatisfied.BindDynamic(this, &UQuestManagerSubsystem::HandleOnNodeTriggerSatisfied);
                
                // Pre-warm target classes so HandleOnNodeStarted's hot-path .Get() skips the LoadSynchronous
                // stall when the step activates. The engine's loaded-asset cache keeps async-loaded UClasses
                // resident - the completion callback is a no-op because the load itself is the side effect we
                // care about. Cold fallback at HandleOnNodeStarted handles the edge case where activation
                // outraces the pre-warm (rare with normal graph activation cadence).
                for (const TSoftClassPtr<AActor>& SoftClass : Step->GetTargetClasses())
                {
                    if (!SoftClass.IsNull() && SoftClass.Get() == nullptr)
                    {
                        AsyncLoadAndActivateClass<AActor>(this, SoftClass, [](UClass* Loaded) {});
                    }
                }
            }

            // Push structural info to the state subsystem. Single lookup serves both the KnownQuests registration
            // (every valid quest tag - answers GetQuestTagsUnderPrefix for hierarchical catch-up subscribers and
            // IsKnownQuestTag for runtime-instance presence) and the container classification (drives the blocker
            // query's AlreadyLive split). Mirrors the existing record-pushes pattern (RecordResolution / RecordEntry /
            // UpdateQuestPrereqStatus) - manager pushes structural info; state subsystem owns the public read surface.
            if (ResolvedTag.IsValid())
            {
                // Centralized: KnownQuests + alias mapping + container classification + display data, for canonical
                // and every AssetScopedAliasTag the instance carries. The helper handles all perspective bookkeeping
                // so this site doesn't have to mirror it.
                RegisterAllNodePerspectives(Instance);
            }
            ++NewlyRegistered;
        }
    }

    // Asset-level display data - RegisterAllNodePerspectives writes per-NODE display data, but the
    // questline ASSET (UQuestlineGraph) carries its own DisplayName / Description / DisplayData fields that need
    // their own write to QSS under the questline's own tag. Without this, adopters querying GetDisplayName /
    // GetDisplayDescription / GetDisplayData on the questline tag get an empty record (or a stale leftover from
    // some unrelated registry write); the asset's authored fields are silently dropped.
    if (UQuestStateSubsystem* StateSubsystem = GetGameInstance() ? GetGameInstance()->GetSubsystem<UQuestStateSubsystem>() : nullptr)
    {
        const FString QuestlineTagString = FQuestTagComposer::IdentityNamespace + Graph->GetQuestlineID();
        const FGameplayTag QuestlineTag = FGameplayTag::RequestGameplayTag(FName(*QuestlineTagString), false);
        if (QuestlineTag.IsValid())
        {
            StateSubsystem->RegisterQuestTag(QuestlineTag);
            StateSubsystem->RegisterDisplayData(QuestlineTag, Graph->GetAuthoredDisplayName(), Graph->GetDescription(), Graph->GetDisplayData());
        }
        else
        {
            UE_LOG(LogSimpleQuestActivation, Warning,
                TEXT("RegisterQuestlineGraph: '%s' - composed questline tag '%s' isn't registered; asset-level display data not stored. "
                     "Adopters querying display data on this tag will receive empty + Warning."),
                *Graph->GetName(), *QuestlineTagString);
        }
    }
    
    UE_LOG(LogSimpleQuestActivation, Log, TEXT("RegisterQuestlineGraph: '%s' - registered %d new node instance(s); skipped %d already registered (FName)"),
        *Graph->GetName(),
        NewlyRegistered,
        SkippedAlreadyRegistered);

    // Reachability walk: identify listener-graphs reachable from this graph's outward setters and async-load them.
    // Cascade-naturally: each newly-loaded graph's RegisterQuestlineGraph triggers its own WarmReachableGraphs, fan-
    // ning out until the closure is reached. KnownLoadedGraphPaths gates cycles + parallel-fan-in.
    WarmReachableGraphs(Graph);
}

FGameplayTag UQuestManagerSubsystem::ResolveToCanonicalTag(FGameplayTag InputTag) const
{
    if (!InputTag.IsValid()) return FGameplayTag();

    if (TObjectPtr<UQuestNodeBase> const* InstancePtr = LoadedNodeInstances.Find(InputTag.GetTagName()))
    {
        if (*InstancePtr)
        {
            const FGameplayTag Canonical = (*InstancePtr)->GetContextualTag();
            if (Canonical.IsValid()) return Canonical;
        }
    }
    return InputTag;
}

FGameplayTag UQuestManagerSubsystem::ResolveSingleCanonicalForMutation(FGameplayTag InputTag) const
{
    if (!InputTag.IsValid() || !QuestStateSubsystem) return FGameplayTag();

    const TArray<FGameplayTag> Canonicals = QuestStateSubsystem->ResolveCanonicalTags(InputTag);
    if (Canonicals.Num() == 1) return Canonicals[0];

    UE_LOG(LogSimpleQuestActivation, Warning,
        TEXT("Mutation request on '%s' resolves to %d placements - class-channel mutations are instance-specific; "
             "address a single placement by its contextual tag. Request ignored."),
        *InputTag.ToString(),
        Canonicals.Num());
    return FGameplayTag();
}

void UQuestManagerSubsystem::AddStateFactAcrossPerspectives(FGameplayTag InputTag, EQuestStateLeaf Leaf)
{
    if (!WorldState || !InputTag.IsValid()) return;

    const FGameplayTag CanonicalTag = ResolveToCanonicalTag(InputTag);
    if (!CanonicalTag.IsValid()) return;

    const FGameplayTag CanonicalFact = FQuestTagComposer::ResolveStateFactTag(CanonicalTag, Leaf);
    if (CanonicalFact.IsValid()) WorldState->AddFact(CanonicalFact);

    if (UQuestNodeBase* Instance = LoadedNodeInstances.FindRef(CanonicalTag.GetTagName()))
    {
        for (const FGameplayTag& AliasTag : Instance->GetAssetScopedAliasTags())
        {
            if (AliasTag.IsValid() && AliasTag != CanonicalTag)
            {
                const FGameplayTag AliasFact = FQuestTagComposer::ResolveStateFactTag(AliasTag, Leaf);
                if (AliasFact.IsValid()) WorldState->AddFact(AliasFact);
            }
        }
    }
}

void UQuestManagerSubsystem::RemoveStateFactAcrossPerspectives(FGameplayTag InputTag, EQuestStateLeaf Leaf)
{
    if (!WorldState || !InputTag.IsValid()) return;

    const FGameplayTag CanonicalTag = ResolveToCanonicalTag(InputTag);
    if (!CanonicalTag.IsValid()) return;

    const FGameplayTag CanonicalFact = FQuestTagComposer::ResolveStateFactTag(CanonicalTag, Leaf);
    if (CanonicalFact.IsValid()) WorldState->RemoveFact(CanonicalFact);

    if (UQuestNodeBase* Instance = LoadedNodeInstances.FindRef(CanonicalTag.GetTagName()))
    {
        for (const FGameplayTag& AliasTag : Instance->GetAssetScopedAliasTags())
        {
            if (AliasTag.IsValid() && AliasTag != CanonicalTag)
            {
                const FGameplayTag AliasFact = FQuestTagComposer::ResolveStateFactTag(AliasTag, Leaf);
                if (AliasFact.IsValid()) WorldState->RemoveFact(AliasFact);
            }
        }
    }
}

void UQuestManagerSubsystem::MarkQuestStarted(FGameplayTag QuestTag)
{
    if (!WorldState || !QuestTag.IsValid()) return;

    // Append-only "has gone Live at least once" anchor - the past-tense sibling of the transient Live fact. Live is
    // re-derived away when a container's last active child finishes; Started is never removed, so catch-up can still
    // reconstruct this node's Started/Activated after a save taken in that limbo. Guarded to a boolean anchor: added
    // once so repeat live transitions don't inflate the ref-count (unlike Completed, whose count is meaningful). The
    // perspectives move in lockstep, so a canonical-presence check gates the whole multi-perspective write.
    const FGameplayTag CanonicalTag = ResolveToCanonicalTag(QuestTag);
    if (!CanonicalTag.IsValid()) return;

    const FGameplayTag StartedFact = FQuestTagComposer::ResolveStateFactTag(CanonicalTag, EQuestStateLeaf::Started);
    if (StartedFact.IsValid() && WorldState->HasFact(StartedFact)) return;   // already anchored - keep it boolean

    AddStateFactAcrossPerspectives(QuestTag, EQuestStateLeaf::Started);
}

void UQuestManagerSubsystem::AddPathFactAcrossPerspectives(FGameplayTag InputTag, FName PathIdentity, const FOriginatingEventID& OriginatingEventID)
{
    if (!WorldState || !InputTag.IsValid() || PathIdentity.IsNone()) return;

    const FGameplayTag CanonicalTag = ResolveToCanonicalTag(InputTag);
    if (!CanonicalTag.IsValid()) return;

    // The state registry holds the identity of the resolution that wrote each mirror fact, so a node woken by one
    // can recover it. Stamp before AddFact: AddFact broadcasts synchronously, so the woken node must be able to read
    // the identity the instant it fires.
    UQuestStateSubsystem* StateSubsystem = GetGameInstance() ? GetGameInstance()->GetSubsystem<UQuestStateSubsystem>() : nullptr;

    const FGameplayTag CanonicalFact = FQuestTagComposer::ResolvePathFactTag(CanonicalTag, PathIdentity);
    if (CanonicalFact.IsValid())
    {
        if (StateSubsystem) StateSubsystem->StampPathFactWriteEventID(CanonicalFact, OriginatingEventID);
        WorldState->AddFact(CanonicalFact);
    }

    if (UQuestNodeBase* Instance = LoadedNodeInstances.FindRef(CanonicalTag.GetTagName()))
    {
        for (const FGameplayTag& AliasTag : Instance->GetAssetScopedAliasTags())
        {
            if (AliasTag.IsValid() && AliasTag != CanonicalTag)
            {
                const FGameplayTag AliasFact = FQuestTagComposer::ResolvePathFactTag(AliasTag, PathIdentity);
                if (AliasFact.IsValid())
                {
                    if (StateSubsystem) StateSubsystem->StampPathFactWriteEventID(AliasFact, OriginatingEventID);
                    WorldState->AddFact(AliasFact);
                }
            }
        }
    }
}

void UQuestManagerSubsystem::ClearPathFactAcrossPerspectives(FGameplayTag InputTag, FName PathIdentity)
{
    if (!WorldState || !InputTag.IsValid() || PathIdentity.IsNone()) return;

    const FGameplayTag CanonicalTag = ResolveToCanonicalTag(InputTag);
    if (!CanonicalTag.IsValid()) return;

    // Drop the recorded write-identity alongside the fact so the map doesn't retain a stale entry after a reset.
    UQuestStateSubsystem* StateSubsystem = GetGameInstance() ? GetGameInstance()->GetSubsystem<UQuestStateSubsystem>() : nullptr;

    const FGameplayTag CanonicalFact = FQuestTagComposer::ResolvePathFactTag(CanonicalTag, PathIdentity);
    if (CanonicalFact.IsValid())
    {
        if (StateSubsystem) StateSubsystem->ClearPathFactWriteEventID(CanonicalFact);
        WorldState->ClearFact(CanonicalFact);
    }

    if (UQuestNodeBase* Instance = LoadedNodeInstances.FindRef(CanonicalTag.GetTagName()))
    {
        for (const FGameplayTag& AliasTag : Instance->GetAssetScopedAliasTags())
        {
            if (AliasTag.IsValid() && AliasTag != CanonicalTag)
            {
                const FGameplayTag AliasFact = FQuestTagComposer::ResolvePathFactTag(AliasTag, PathIdentity);
                if (AliasFact.IsValid())
                {
                    if (StateSubsystem) StateSubsystem->ClearPathFactWriteEventID(AliasFact);
                    WorldState->ClearFact(AliasFact);
                }
            }
        }
    }
}

void UQuestManagerSubsystem::ResetQuestRunState(FGameplayTag QuestTag)
{
    if (!QuestTag.IsValid() || !WorldState)
    {
        return;
    }

    // Never wipe an in-flight run's mirrors: a currently-Live quest is mid-progress and its mirrors are load-bearing.
    const FGameplayTag LiveFact = FQuestTagComposer::ResolveStateFactTag(QuestTag, EQuestStateLeaf::Live);
    if (LiveFact.IsValid() && WorldState->HasFact(LiveFact))
    {
        return;
    }

    UGameInstance* GI = GetGameInstance();
    UQuestStateSubsystem* Registry = GI ? GI->GetSubsystem<UQuestStateSubsystem>() : nullptr;
    if (!Registry)
    {
        return;
    }

    // Clear the clearable projection for every distinct path this quest has resolved through. The append-only
    // resolution registry and the Completed anchor are never touched - only the mirror that resettable gates read.
    TSet<FName> ClearedPaths;
    for (const FQuestResolutionEntry& Entry : Registry->GetResolutionHistory(QuestTag))
    {
        if (!Entry.PathIdentity.IsNone() && !ClearedPaths.Contains(Entry.PathIdentity))
        {
            ClearPathFactAcrossPerspectives(QuestTag, Entry.PathIdentity);
            ClearedPaths.Add(Entry.PathIdentity);
        }
    }
}

void UQuestManagerSubsystem::RegisterLoadedNodeInstance(FName Key, UQuestNodeBase* Instance)
{
    if (Key.IsNone() || !Instance) return;

    if (TObjectPtr<UQuestNodeBase>* ExistingPtr = LoadedNodeInstances.Find(Key))
    {
        if (*ExistingPtr != Instance)
        {
            UE_LOG(LogSimpleQuestActivation, Warning,
                TEXT("RegisterLoadedNodeInstance: key '%s' already maps to a different Instance ('%s' vs incoming '%s') - ")
                TEXT("alias keys must be unique per Instance for dedup-by-pointer to work correctly. Preserving existing mapping."),
                *Key.ToString(),
                ExistingPtr->Get() ? *ExistingPtr->Get()->GetName() : TEXT("<null>"),
                *Instance->GetName());
        }
        return;
    }
    LoadedNodeInstances.Add(Key, Instance);
}

void UQuestManagerSubsystem::RegisterAllNodePerspectives(const UQuestNodeBase* Instance) const
{
    if (!Instance) return;

    const FGameplayTag Canonical = Instance->GetContextualTag();
    if (!Canonical.IsValid()) return;

    UQuestStateSubsystem* StateSubsystem = GetGameInstance() ? GetGameInstance()->GetSubsystem<UQuestStateSubsystem>() : nullptr;
    if (!StateSubsystem) return;

    const bool bIsContainer = Instance->IsContainerNode();
    const FText& InDisplayName = Instance->GetDisplayName();
    const FText& InDescription = Instance->GetDescription();
    UQuestDisplayData* InDisplayData = Instance->GetDisplayData();

    // Canonical: register directly. RegisterAlias would bail on equal tags so we use RegisterQuestTag explicitly.
    StateSubsystem->RegisterQuestTag(Canonical);
    if (bIsContainer)
    {
        StateSubsystem->RegisterContainerTag(Canonical);
    }
    StateSubsystem->RegisterDisplayData(Canonical, InDisplayName, InDescription, InDisplayData);

    // Aliases: register each as a perspective of Canonical. RegisterAlias folds in RegisterQuestTag internally.
    for (const FGameplayTag& AliasTag : Instance->GetAssetScopedAliasTags())
    {
        if (!AliasTag.IsValid()) continue;
        StateSubsystem->RegisterAlias(AliasTag, Canonical);
        if (bIsContainer)
        {
            StateSubsystem->RegisterContainerTag(AliasTag);
        }
        StateSubsystem->RegisterDisplayData(AliasTag, InDisplayName, InDescription, InDisplayData);
    }
}

void UQuestManagerSubsystem::ActivateQuestlineGraph(UQuestlineGraph* Graph, const FQuestObjectiveActivationContext& Params)
{
    if (!Graph) return;

    // Resolve the questline's own identity tag up front - used by the idempotency gate here and the asset-level
    // lifecycle publish further down.
    const FString QuestlineTagString = FQuestTagComposer::IdentityNamespace + Graph->GetQuestlineID();
    const FGameplayTag QuestlineTag = FGameplayTag::RequestGameplayTag(FName(*QuestlineTagString), false);

    // Idempotent start - a fresh activation on a questline that has already begun in this session would clobber it,
    // whether it's still running or was restored from a save. The append-only Started anchor is the signal: written on
    // first activation, never cleared (so it survives inner-node limbo and completion), and restored by ApplyQuestSnapshot
    // BEFORE the level reloads - so it reads true whether Start Questline fires at level BeginPlay (before the restore
    // flush) or later from a debug key / interaction (after it). A new game leaves it unset, so the first start proceeds.
    // Net effect: Start Questline is safe to call unconditionally; it starts a questline once and won't fight a load.
    if (QuestlineTag.IsValid() && FQuestLifecycleQuery::IsStarted(WorldState, QuestlineTag))
    {
        UE_LOG(LogSimpleQuestActivation, Log,
            TEXT("ActivateQuestlineGraph: '%s' already started (running or restored from a save) - skipping fresh activation."),
            *Graph->GetName());
        return;
    }

    const FSoftObjectPath GraphPath(Graph);

    // Restore deferral - if a save restore is going to reconstruct this graph on the current level load, skip the fresh
    // activation so it can't race (and clobber) the restore. ApplyQuestSnapshot stashes PendingRestoreGraphs BEFORE
    // OpenLevel; RestorePendingGraphs consumes it a tick AFTER the reloaded world's BeginPlay - so a StartQuestline call
    // in a pawn/level BeginPlay fires inside that window, sees the graph still pending here, and yields. New game (no
    // snapshot applied) leaves the set empty, so activation proceeds normally. Net effect: StartQuestline is safe to call
    // unconditionally on level start - it either starts fresh or defers to the restore, with no branching at the caller.
    if (PendingRestoreGraphs.Contains(GraphPath))
    {
        UE_LOG(LogSimpleQuestActivation, Log,
            TEXT("ActivateQuestlineGraph: '%s' is pending a save restore this level load - skipping fresh activation; the restore reconstructs it."),
            *Graph->GetName());
        return;
    }
    
    // Cycle break - if this graph is already inside an in-flight activation cascade, skip to prevent the loop.
    // Catches both direct self-reference (Start Questline node in graph X targeting X) and indirect cycles
    // (A → B → A or longer). The set is keyed by soft path so it stays valid across async-load boundaries.
    if (ActivatingGraphPaths.Contains(GraphPath))
    {
        UE_LOG(LogSimpleQuestActivation, Warning,
            TEXT("ActivateQuestlineGraph: cycle detected - graph '%s' is already inside an activation cascade. ")
            TEXT("Skipping to break the loop. Likely a self-referencing Start Questline node or a multi-graph cycle (e.g., A→B→A)."),
            *Graph->GetName());
        return;
    }

    ActivatingGraphPaths.Add(GraphPath);
    ON_SCOPE_EXIT { ActivatingGraphPaths.Remove(GraphPath); };

    RegisterQuestlineGraph(Graph);

    // Asset-level activation publish on the questline's own tag channel. Symmetric with PublishGraphResolutions's
    // close-out publish. Payload carries the questline's identity + forwards Instigator / CustomData from the caller's
    // activation params so adopters can attribute "who started this questline." PrereqStatus is default-constructed:
    // asset-level activation doesn't have a prereq concept (gating lives on individual content nodes inside the graph).
    if (QuestSignalSubsystem)
    {
        if (QuestlineTag.IsValid())
        {
            FQuestEventPayload Payload;
            Payload.NodeInfo.QuestTag = QuestlineTag;
            Payload.Instigator = Params.Instigator;
            Payload.CustomData = Params.CustomData;
            QuestSignalSubsystem->PublishMessage<FQuestActivatedEvent>(
                QuestlineTag,
                FQuestActivatedEvent(QuestlineTag, Payload, FQuestPrereqStatus{}));

            // Asset-level Started publish - parallel to the Activated above. For asset-level, scope entry IS the
            // Live transition (assets don't go through a giver gate), so Activated and Started fire together
            // here. Adopters who bind to OnStarted on the questline tag (triggers, mission-active UI, etc.) get
            // the asset-level Live signal alongside subscribers bound to OnActivated.
            QuestSignalSubsystem->PublishMessage<FQuestStartedEvent>(
                QuestlineTag,
                FQuestStartedEvent(QuestlineTag, Payload, nullptr));

            // Asset-level Live fact write - symmetric with PublishGraphResolutions's Completed fact write at
            // resolution (§4.36). Persists past the transient publishes above so late subscribers reconstruct
            // Activated + Started via UQuestLifecycleObserver's catch-up. Uses AddStateFactAcrossPerspectives
            // (matching the close-out pattern) to handle alias forms; for top-level asset tags this is
            // effectively a single-tag write.
            AddStateFactAcrossPerspectives(QuestlineTag, EQuestStateLeaf::Live);
            MarkQuestStarted(QuestlineTag);
        }
        else
        {
            UE_LOG(LogSimpleQuestActivation, Warning,
                TEXT("ActivateQuestlineGraph: '%s' - composed tag '%s' isn't registered with the runtime tag manager. "
                     "Asset-level Activated publish skipped; adopters bound on the questline tag won't receive a start signal."),
                *Graph->GetName(),
                *QuestlineTagString);
        }
    }

    UE_LOG(LogSimpleQuestActivation, Log, TEXT("ActivateQuestlineGraph: '%s' - firing %d entry tag(s) (CustomData %s, Instigator %s)"),
        *Graph->GetName(), Graph->GetEntryNodeTags().Num(),
        Params.CustomData.IsValid() ? TEXT("populated") : TEXT("empty"),
        Params.Instigator.IsValid() ? *Params.Instigator->GetName() : TEXT("null"));

    for (const FName& EntryTagName : Graph->GetEntryNodeTags())
    {
        // Mirror of HandleActivationRequest / HandleGiveQuestEvent: stash Params on each entry Step's
        // PendingActivationContext before activation, so ActivateInternal merges them with the Step's authored
        // defaults. Empty Params stamps cleanly; ActivateInternal's additive merge preserves authored defaults
        // in that case. Container entry nodes don't carry an activation context themselves - Params propagates
        // to the first Step reached on the inward cascade.
        if (UQuestNodeBase* Instance = LoadedNodeInstances.FindRef(EntryTagName))
        {
            if (UQuestStep* Step = Cast<UQuestStep>(Instance))
            {
                Step->PendingActivationContext.IncomingContext = Params;
            }
        }

        ActivateNodeByTag(EntryTagName, EQuestActivationProvenance::InitialEntry);
    }
}

void UQuestManagerSubsystem::RestoreQuestlineGraph(UQuestlineGraph* Graph)
{
    if (!Graph) return;

    RegisterQuestlineGraph(Graph);

    UQuestStateSubsystem* StateSubsystem = GetGameInstance() ? GetGameInstance()->GetSubsystem<UQuestStateSubsystem>() : nullptr;

    int32 RestoredCount = 0;
    int32 RearmedCount = 0;
    for (const auto& Pair : Graph->GetCompiledNodes())
    {
        UQuestNodeBase* Node = Pair.Value;
        if (!Node) continue;

        // Re-arm a prereq-deferred activation - keyed by QuestContentGuid so it covers TAG-LESS utility nodes (a Prereq
        // Gate defers with an invalid contextual tag) as well as content nodes. Restore the pending context, then replay
        // Activate: it re-evaluates the prereq against the restored facts and re-defers (or fires if already satisfied).
        // Activate takes the node's contextual tag - valid for content, invalid-but-harmless for a gate (it just forwards).
        const FGuid NodeGuid = Node->GetQuestGuid();
        if (NodeGuid.IsValid())
        {
            if (const FQuestObjectiveRuntimeContext* DeferredCtx = PendingDeferredActivations.Find(NodeGuid))
            {
                Node->PendingActivationContext = *DeferredCtx;
                PendingDeferredActivations.Remove(NodeGuid);
                Node->Activate(Node->GetContextualTag());
                ++RearmedCount;
                UE_LOG(LogSimpleQuestActivation, Log, TEXT("RestoreQuestlineGraph: '%s' - re-armed deferred activation for '%s'"),
                    *Graph->GetName(),
                    Node->GetContextualTag().IsValid() ? *Node->GetContextualTag().ToString() : *NodeGuid.ToString(EGuidFormats::Short));
                continue;
            }
        }

        // Rebuild the live objective on Steps the restored WorldState marks Live.
        UQuestStep* Step = Cast<UQuestStep>(Node);
        if (!Step) continue;   // Only Steps own a live objective; container Live derives from inner Step state.

        const FGameplayTag NodeTag = Node->GetContextualTag();
        if (!NodeTag.IsValid()) continue;

        const FGameplayTag LiveFact = FQuestTagComposer::ResolveStateFactTag(NodeTag, EQuestStateLeaf::Live);
        if (!LiveFact.IsValid() || !WorldState || !WorldState->HasFact(LiveFact)) continue;

        if (Step->GetLiveObjective())
        {
            UE_LOG(LogSimpleQuestActivation, Verbose, TEXT("RestoreQuestlineGraph: '%s' - Step '%s' already has a live objective; skipping"),
                *Graph->GetName(), *NodeTag.ToString());
            continue;
        }

        FQuestObjectiveActivationContext IncomingContext;
        if (StateSubsystem)
        {
            IncomingContext = StateSubsystem->GetLatestEntry(NodeTag).ActivationContextSnapshot;
        }

        Step->RestoreObjective(IncomingContext, NodeTag);

        if (const FSimpleQuestObjectiveSaveState* ObjState = PendingObjectiveStates.Find(Step->GetQuestGuid()))
        {
            if (UQuestObjective* Objective = Step->GetLiveObjective())
            {
                Objective->RestoreObjectiveState(*ObjState);
            }
            PendingObjectiveStates.Remove(Step->GetQuestGuid());
        }

        WireStepTriggerSubscriptions(Step);
        ++RestoredCount;

        UE_LOG(LogSimpleQuestActivation, Log, TEXT("RestoreQuestlineGraph: '%s' - restored live objective for Step '%s'"),
            *Graph->GetName(), *NodeTag.ToString());
    }

    UE_LOG(LogSimpleQuestActivation, Log, TEXT("RestoreQuestlineGraph: '%s' - reconstituted %d live objective(s), re-armed %d deferred activation(s)"),
        *Graph->GetName(), RestoredCount, RearmedCount);
}

void UQuestManagerSubsystem::RestorePendingGraphs()
{
    const TArray<FSoftObjectPath> Graphs = ConsumePendingRestoreGraphs();
    UE_LOG(LogSimpleQuestActivation, Log, TEXT("RestorePendingGraphs: restoring %d stashed graph(s)."), Graphs.Num());
    for (const FSoftObjectPath& Path : Graphs)
    {
        AsyncLoadAndActivate<UQuestlineGraph>(this, TSoftObjectPtr<UQuestlineGraph>(Path),
            [this](UQuestlineGraph* Graph)
            {
                if (Graph)
                {
                    RestoreQuestlineGraph(Graph);
                }
                else
                {
                    UE_LOG(LogSimpleQuestActivation, Warning, TEXT("RestorePendingGraphs: a stashed graph failed to load; skipped."));
                }
            });
    }
}

void UQuestManagerSubsystem::ArmRestoreOnNextLevelLoad()
{
    if (bRestoreArmed) return;   // idempotent - a second Apply(true) before a load re-uses the existing arm
    bRestoreArmed = true;
    ArmedFromWorld = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
    PostWorldInitHandle = FWorldDelegates::OnPostWorldInitialization.AddUObject(this, &UQuestManagerSubsystem::HandleWorldInitForRestore);
    UE_LOG(LogSimpleQuestActivation, Log, TEXT("ArmRestoreOnNextLevelLoad: armed - pending restore flushes when the next game world initializes."));
}

void UQuestManagerSubsystem::DisarmRestoreOnNextLevelLoad()
{
    if (PostWorldInitHandle.IsValid())
    {
        FWorldDelegates::OnPostWorldInitialization.Remove(PostWorldInitHandle);
        PostWorldInitHandle.Reset();
    }
    bRestoreArmed = false;
    ArmedFromWorld = nullptr;
}

void UQuestManagerSubsystem::HandleWorldInitForRestore(UWorld* World, const UWorld::InitializationValues IVS)
{
    if (!bRestoreArmed || !World) return;

    // Target the GAME/PIE world only (skip editor/preview/inactive), and skip the world we armed IN - its init already
    // fired before we armed, so the next fresh game-world init is the OpenLevel target.
    if (World->WorldType != EWorldType::Game && World->WorldType != EWorldType::PIE) return;
    if (World == ArmedFromWorld.Get()) return;

    UE_LOG(LogSimpleQuestActivation, Log, TEXT("HandleWorldInitForRestore: target world '%s' initialized - flushing pending restore next tick."),
        *World->GetName());

    DisarmRestoreOnNextLevelLoad();   // one-shot

    // Defer one tick so the loaded world is fully current before objectives rebuild in it - a hot-loaded graph would
    // otherwise restore synchronously during world init, before the game instance's world pointer settles.
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this,
        [this](float) { RestorePendingGraphs(); return false; }));
}

TMap<FGuid, FQuestObjectiveRuntimeContext> UQuestManagerSubsystem::CaptureDeferredActivations() const
{
    TMap<FGuid, FQuestObjectiveRuntimeContext> Out;
    TSet<const UQuestNodeBase*> Seen;
    for (const TPair<FName, TObjectPtr<UQuestNodeBase>>& Pair : LoadedNodeInstances)
    {
        UQuestNodeBase* Node = Pair.Value;
        if (!Node || Seen.Contains(Node)) continue;   // alias-duplicate keys; visit each node once
        Seen.Add(Node);

        // Armed-and-waiting on a prereq - the reliable signal for BOTH content and tag-less utility nodes (a Prereq Gate
        // defers with an invalid DeferredContextualTag, which the old tag check missed).
        if (!Node->IsAwaitingPrerequisite()) continue;

        const FGuid NodeGuid = Node->GetQuestGuid();
        if (!NodeGuid.IsValid()) continue;   // stable per-placement save key (now set on util nodes too)
        Out.FindOrAdd(NodeGuid) = Node->PendingActivationContext;
    }
    return Out;
}

TMap<FGuid, FSimpleQuestObjectiveSaveState> UQuestManagerSubsystem::CaptureObjectiveStates() const
{
    TMap<FGuid, FSimpleQuestObjectiveSaveState> Out;
    TSet<const UQuestNodeBase*> Seen;
    for (const TPair<FName, TObjectPtr<UQuestNodeBase>>& Pair : LoadedNodeInstances)
    {
        UQuestStep* Step = Cast<UQuestStep>(Pair.Value);
        if (!Step || Seen.Contains(Step)) continue;   // alias-duplicate keys; visit each Step once
        Seen.Add(Step);

        UQuestObjective* Objective = Step->GetLiveObjective();
        if (!Objective) continue;

        const FSimpleQuestObjectiveSaveState State = Objective->CaptureObjectiveState();
        if (State.bHasState && Step->GetQuestGuid().IsValid())
        {
            Out.Add(Step->GetQuestGuid(), State);
        }
    }
    return Out;
}

FQuestEventPayload UQuestManagerSubsystem::AssembleEventContext(const UQuestNodeBase* Node, const FQuestObjectiveTriggerContext& InCompletionTrigger) const
{
    FQuestEventPayload Context;
    Context.NodeInfo = Node->GetNodeInfo();
    Context.CompletionTrigger = InCompletionTrigger;

    // Forward the FQuestContextBase fields from the Step's merged activation context - without this, the
    // outbound payload arrives with empty Instigator / CustomData / OriginTag / OriginChain even when
    // ActivateQuest / GiveQuest / etc. passed populated Params. UQuestStep::ActivateInternal stamps the
    // merged context into ReceivedActivationContext before Super fires OnNodeStarted, so by the time this
    // helper runs on the publish path the data is ready to forward. Closes the read-from half of the
    // bidirectional adopter pipeline for attribution data.
    if (const UQuestStep* Step = Cast<UQuestStep>(Node))
    {
        // Read from Pending when it carries data, Received otherwise. Pending is stamped by upstream callers
        // (ChainToNextNodes, HandleGiveQuestEvent, HandleActivationRequest) before Activate runs and is the only
        // populated buffer at giver-gate time (Activated / Enabled fire from ActivateNodeByTag's GiverGateFire
        // branch without invoking ActivateInternal). Received is populated by UQuestStep::ActivateInternal's
        // merge before Super fires OnNodeStarted; Started reads either (both hold the same data at that point
        // - Pending isn't cleared until the tail of ActivateInternal, after Super returns). Post-Started events
        // (Progress / Completed / Deactivated) fire after Pending has been cleared, so Received is the surviving
        // source. PendingActivationContext is protected; the manager has friend access.
        const FQuestObjectiveRuntimeContext& Pending = Step->PendingActivationContext;
        const bool bPendingHasData = Pending.IncomingContext.Instigator.IsValid()
            || Pending.IncomingContext.CustomData.IsValid()
            || Pending.IncomingContext.OriginTag.IsValid()
            || !Pending.IncomingContext.OriginChain.IsEmpty();

        const FQuestObjectiveRuntimeContext& Source = bPendingHasData ? Pending : Step->GetReceivedActivationParams();

        Context.Instigator = Source.IncomingContext.Instigator;
        Context.CustomData = Source.IncomingContext.CustomData;
        Context.OriginTag = Source.IncomingContext.OriginTag;
        Context.OriginChain = Source.IncomingContext.OriginChain;
        Context.OriginatingEventID = Source.IncomingContext.OriginatingEventID;
    }

    // Completion / progress events attribute to whoever completed or advanced the node (the trigger), not who activated
    // it. Only those events pass a populated CompletionTrigger; activation-side events pass an empty one and keep the
    // activation attribution above. Lineage (Origin*) stays activation-sourced.
    if (Context.CompletionTrigger.TriggeredActor
        || Context.CompletionTrigger.Instigator.IsValid()
        || Context.CompletionTrigger.CustomData.IsValid()
        || Context.CompletionTrigger.CustomTag.IsValid())
    {
        Context.Instigator = Context.CompletionTrigger.Instigator;
        Context.CustomData = Context.CompletionTrigger.CustomData;
        Context.CustomTag  = Context.CompletionTrigger.CustomTag;
    }
    
    UE_LOG(LogSimpleQuestActivation, Verbose, TEXT("AssembleEventContext: '%s' DisplayName='%s' CompletionContext=%s Instigator=%s CustomData=%s"),
        *Context.NodeInfo.QuestTag.ToString(),
        *Context.NodeInfo.DisplayName.ToString(),
        Context.CompletionTrigger.TriggeredActor ? TEXT("set") : TEXT("empty"),
        Context.Instigator.IsValid() ? *Context.Instigator->GetName() : TEXT("none"),
        Context.CustomData.IsValid() ? TEXT("populated") : TEXT("empty"));

    return Context;
}

void UQuestManagerSubsystem::HandleOnNodeCompleted(UQuestNodeBase* Node, FGameplayTag OutcomeTag, FName PathIdentity)
{
    UE_LOG(LogSimpleQuestActivation, Log, TEXT("HandleOnNodeCompleted: '%s' outcome='%s' path='%s'"),
        *Node->GetContextualTag().ToString(),
        *OutcomeTag.ToString(),
        *PathIdentity.ToString());

    UQuestStep* Step = Cast<UQuestStep>(Node);
    // Anything other than Satisfied defers the chain. Indeterminate means a leaf's source is held - the completion is
    // real and its fact is written, but its consequences have not been released, so this step's chain waits with it.
    const auto IsSourceHeld = [this](FGameplayTag Source) { return IsQuestAdvancementHeld(Source); };
    if (Step
        && !Step->IsGiverGated()
        && Step->GetPrerequisiteGateMode() == EPrerequisiteGateMode::GatesCompletion
        && !Step->PrerequisiteExpression.IsAlways()
        && Step->PrerequisiteExpression.EvaluateWithHolds(WorldState, QuestStateSubsystem, IsSourceHeld) != EPrereqTriState::Satisfied)
    {
        UE_LOG(LogSimpleQuestActivation, Verbose, TEXT("HandleOnNodeCompleted: '%s' - prereqs unmet, deferring chain"), *Node->GetContextualTag().ToString());
        DeferChainToNextNodes(Step, OutcomeTag, PathIdentity);
        return;
    }

    // Mint the cascade event ID at the originating Step's completion. Multi-tag-stable AuthoredNodeGuid (same
    // authored Step in two compile contexts shares it) + per-tick-stable timestamp distinguishes cascades from
    // genuinely different gameplay events. Threaded through ChainToNextNodes onto every destination's
    // PendingActivationContext and into every FireWrapperBoundaryCompletion call.
    FOriginatingEventID OriginatingEventID;
    OriginatingEventID.AuthoredNodeGuid = Node->GetAuthoredNodeGuid();
    OriginatingEventID.ResolutionTimestamp = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
    UE_LOG(LogSimpleQuestActivation, Verbose,
        TEXT("HandleOnNodeCompleted: '%s' minted cascade event ID - guid=%s ts=%.3f"),
        *Node->GetContextualTag().ToString(),
        *OriginatingEventID.AuthoredNodeGuid.ToString(EGuidFormats::Short),
        OriginatingEventID.ResolutionTimestamp);

    ChainToNextNodes(Node, OutcomeTag, PathIdentity, OriginatingEventID);
}

void UQuestManagerSubsystem::HandleOnNodeProgress(UQuestStep* Step, FQuestObjectiveTriggerContext ProgressData)
{
    if (!Step || !QuestSignalSubsystem) return;

    UE_LOG(LogSimpleQuestActivation, Verbose, TEXT("HandleOnNodeProgress: '%s' - %d/%d"),
        *Step->GetContextualTag().ToString(),
        ProgressData.CurrentCount,
        ProgressData.RequiredCount);

    FQuestEventPayload Context = AssembleEventContext(Step, ProgressData);
    FQuestPublish::OnAllNodeTags(QuestSignalSubsystem, Step, FQuestProgressEvent(Step->GetContextualTag(), Context));

    // Trigger-side per-fire Response. Echoes the originating trigger fire's TriggerContext so UQuestTriggerComponent's
    // own-fire filter (TriggeredActor == GetOwner()) resolves. Empty ProgressData (objective called ReportProgress
    // outside a fire context) still publishes - adopters who care filter; those who don't pay only the publishing cost.
    FQuestPublish::OnAllNodeTags(QuestSignalSubsystem, Step, FQuestTriggerResponseEvent(Step->GetContextualTag(), EQuestTriggerResolution::Progress, ProgressData));
}

void UQuestManagerSubsystem::HandleOnNodeRefused(UQuestStep* Step, FGameplayTag RefusalReason, FQuestObjectiveTriggerContext TriggerContext)
{
    if (!Step || !QuestSignalSubsystem) return;

    UE_LOG(LogSimpleQuestActivation, Verbose, TEXT("HandleOnNodeRefused: '%s' reason='%s' triggered='%s'"),
        *Step->GetContextualTag().ToString(),
        *RefusalReason.ToString(),
        TriggerContext.TriggeredActor ? *TriggerContext.TriggeredActor->GetName() : TEXT("(none)"));

    FQuestPublish::OnAllNodeTags(QuestSignalSubsystem, Step, FQuestTriggerResponseEvent(
        Step->GetContextualTag(), EQuestTriggerResolution::Refused, FGameplayTag(), RefusalReason, TriggerContext));
}

void UQuestManagerSubsystem::HandleOnNodeTriggerDeactivation(UQuestStep* Step, FGameplayTag OutcomeTag, FQuestObjectiveTriggerContext FinalContext)
{
    if (!Step || !QuestSignalSubsystem) return;

    UE_LOG(LogSimpleQuestActivation, Verbose, TEXT("HandleOnNodeTriggerDeactivation: '%s' outcome='%s'"),
        *Step->GetContextualTag().ToString(),
        *OutcomeTag.ToString());

    // Manual is the only reason that can reach this handler - Completed and Interrupted publish at their own
    // auto-publish sites in PublishQuestEndedEvent / SetQuestDeactivated. Hardcoded here rather than threaded
    // through the chain so the BP-callable's surface stays honest ("the manual way to do this").
    FQuestPublish::OnAllNodeTags(QuestSignalSubsystem, Step, FQuestTriggerDeactivatedEvent(
        Step->GetContextualTag(), EQuestTriggerEndReason::Manual, OutcomeTag, FinalContext));
}

void UQuestManagerSubsystem::HandleOnNodeTriggerSatisfied(UQuestStep* Step, FQuestObjectiveTriggerContext Context)
{
    if (!Step || !QuestSignalSubsystem) return;

    UE_LOG(LogSimpleQuestActivation, Verbose, TEXT("HandleOnNodeTriggerSatisfied: '%s' satisfiedActor='%s'"),
        *Step->GetContextualTag().ToString(),
        Context.TriggeredActor ? *Context.TriggeredActor->GetName() : TEXT("null"));

    FQuestPublish::OnAllNodeTags(QuestSignalSubsystem, Step, FQuestTriggerSatisfiedEvent(Step->GetContextualTag(), Context.TriggeredActor.Get(), Context));
}

void UQuestManagerSubsystem::WireStepTriggerSubscriptions(UQuestStep* Step)
{
    if (!Step || !QuestSignalSubsystem) return;

    const FGameplayTag StepTag = Step->GetContextualTag();
    if (!StepTag.IsValid()) return;

    FDelegateHandle Handle = QuestSignalSubsystem->SubscribeRawMessage<FQuestTriggerFiredEvent>(StepTag, this, &UQuestManagerSubsystem::CheckQuestObjectives);
    LiveStepTriggerHandles.Add(StepTag, Handle);

    if (!Step->GetTargetClasses().IsEmpty())
    {
        for (const TSoftClassPtr<AActor>& SoftClass : Step->GetTargetClasses())
        {
            // Hot path: target class was pre-warmed at RegisterQuestlineGraph time via AsyncLoadAndActivateClass, so
            // .Get() typically returns the loaded class without stalling the frame. Cold fallback covers the edge case
            // where the step activates before the pre-warm completes; verbose log makes pre-warm gaps visible in traces.
            UClass* Loaded = SoftClass.Get();
            if (!Loaded)
            {
                UE_LOG(LogSimpleQuestActivation, Verbose,
                    TEXT("WireStepTriggerSubscriptions: '%s' target class '%s' not pre-warmed; falling back to LoadSynchronous"),
                    *StepTag.ToString(),
                    *SoftClass.ToSoftObjectPath().ToString());
                Loaded = SoftClass.LoadSynchronous();
            }
            if (Loaded)
            {
                ClassFilteredSteps.Add(StepTag, Loaded);
            }
        }

        // Subscribe once to the global trigger channel if this is the first class-filtered step.
        if (!ClassBridgeHandle.IsValid())
        {
            ClassBridgeHandle = QuestSignalSubsystem->SubscribeRawMessage<FQuestTriggerFiredEvent>(Tag_Channel_QuestTrigger, this, &UQuestManagerSubsystem::CheckClassObjectives);
        }
    }
}

void UQuestManagerSubsystem::HandleOnNodeStarted(UQuestNodeBase* Node, FGameplayTag InContextualTag)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(UQuestManagerSubsystem_HandleOnNodeStarted);

    if (Node->GetContextualTag().IsValid() && Node->IsStepNode())
    {
        // Only Steps own a direct Live fact; containers' Live state is derived from inner Step state by
        // DeriveContainerLive (triggered by the Step's SetQuestLive ancestor walk). Containers still publish
        // FQuestStartedEvent below so subscribers see the activation, just without an accompanying intrinsic Live
        // fact write. The Live fact arrives later when an inner Step activates and walks up.
        SetQuestLive(Node->GetContextualTag());
    }
    if (QuestSignalSubsystem)
    {
        FQuestEventPayload Context = AssembleEventContext(Node, FQuestObjectiveTriggerContext());
        AActor* GiverActor = nullptr;
        
        if (TWeakObjectPtr<AActor>* Found = RecentGiverActors.Find(Node->GetContextualTag()))
        {
            GiverActor = Found->Get();
            RecentGiverActors.Remove(Node->GetContextualTag());
        }

        // Suppress lifecycle-transition events for wrappers re-entering while already Live (loop-back wires,
        // fan-in re-entry, GiverGateSkipPathAware / ContainerReentry decisions). Both QuestActivatedEvent
        // ("entered scope") and QuestStartedEvent ("transitioned to Live") name TRANSITIONS - firing them on
        // a no-op re-entry signals false transitions to subscribers, breaking state machines that pair them
        // as scope/lifecycle markers. For real first-time activation the wrapper isn't yet Live at this point
        // (DeriveContainerLive's ancestor walk runs later in the same call stack), so the guard below
        // correctly publishes. Steps don't need this guard: their RefuseStepAlreadyLive decision returns
        // early in ActivateNodeByTag before reaching HandleOnNodeStarted. Re-entry awareness for designer
        // observation lives in the entry-record events (FQuestEntryRecordedEvent), which still fire below.
        const bool bIsWrapperReentry = Node->IsContainerNode() && FQuestLifecycleQuery::IsLive(WorldState, Node->GetContextualTag());
        if (!bIsWrapperReentry)
        {
            // FQuestActivatedEvent - "this quest is now in scope." Originally fired only at the giver-gate
            // path (where scope means PendingGiver state); broadened so adopters get a universal entry-point
            // signal at every cascade waypoint. Giver-gated paths still fire Activated earlier at the
            // GiverGateFire decision (with the actual evaluated PrereqStatus); skipping the publish here
            // when bWasGiverGated avoids the double-fire. Non-gated paths get Activated alongside Started in
            // the same call - adopters who care about the giver-gating distinction branch on the instance's
            // bWasGiverGated flag or check PrereqStatus.bIsAlways.
            if (!Node->bWasGiverGated)
            {
                // Non-gated paths reach HandleOnNodeStarted only after upstream prereq deferral cleared
                // (DeferActivation gating). By definition prereqs are satisfied here; default-constructed
                // PrereqStatus carries the satisfied = true semantics. Adopters can still call
                // QuestStateSubsystem::QueryQuestActivationBlockers if they want the live snapshot.
                FQuestPublish::OnAllNodeTags(QuestSignalSubsystem, Node, FQuestActivatedEvent(Node->GetContextualTag(), Context, FQuestPrereqStatus{}));
            }
            FQuestPublish::OnAllNodeTags(QuestSignalSubsystem, Node, FQuestStartedEvent(Node->GetContextualTag(), Context, GiverActor));
            
            // Durable Started anchor on scope entry. Writing the anchor here - where the Started event already fires -
            // lets catch-up (which keys Activated/Started replay on Started) reconstruct an entered-but-never-Live
            // container. Idempotent: SetQuestLive's later call no-ops on it.
            MarkQuestStarted(Node->GetContextualTag());
        }
        else
        {
            UE_LOG(LogSimpleQuestActivation, Verbose,
                TEXT("HandleOnNodeStarted: '%s' re-entering while already Live - suppressing QuestActivatedEvent + QuestStartedEvent (no-op re-activation)"),
                *Node->GetContextualTag().ToString());
        }
        
        if (UQuestStep* Step = Cast<UQuestStep>(Node))
        {
            // Wire the trigger→objective bridge (step-tag + class-filtered target subscriptions). Factored so save
            // restore re-establishes the identical wiring for a rebuilt objective (see WireStepTriggerSubscriptions).
            WireStepTriggerSubscriptions(Step);

            // Step-side entry record. Captures every Step start with the merged final params snapshot delivered to the
            // live objective (Step->ReceivedActivationContext). Mirrors the wrapper-side per-cascade RecordEntry in the
            // UQuest branch below - wrapper records "this wrapper was entered by these cascades," Step records "this Step
            // was activated with these merged params." SourceQuestTag / IncomingOutcomeTag come from the snapshot's cascade
            // fields (invalid for non-cascade-driven Step starts). PathIdentity is NAME_None because Steps don't have
            // per-source routing.
            if (QuestStateSubsystem && Node->GetContextualTag().IsValid())
            {
                const FQuestObjectiveRuntimeContext& Snapshot = Step->GetReceivedActivationParams();
                const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
                QuestStateSubsystem->RecordEntry(
                    Node->GetContextualTag(),
                    Snapshot.IncomingContext.OriginTag,
                    Snapshot.IncomingOutcomeTag,
                    Now,
                    Snapshot.Provenance,
                    Snapshot.IncomingContext,
                    NAME_None,
                    Snapshot.IncomingContext.OriginatingEventID);
                UE_LOG(LogSimpleQuestActivation, Verbose, TEXT("HandleOnNodeStarted: recorded Step entry for '%s' provenance=%s giver='%s'"),
                    *Node->GetContextualTag().ToString(),
                    *UEnum::GetValueAsString(Snapshot.Provenance),
                    Snapshot.IncomingContext.Instigator.IsValid() ? *Snapshot.IncomingContext.Instigator->GetName() : TEXT("null"));
            }
        }
    }
    // UQuest inner-entry activation. When Activate defers due to unmet prereqs, this branch doesn't run; when prereqs
    // satisfy (immediately or via TryActivateDeferred firing), ActivateInternal runs, OnNodeStarted fires, this branch
    // runs and drains the per-cascade queue populated by ActivateNodeByTag.
    if (UQuest* QuestNode = Cast<UQuest>(Node))
    {
        const FName NodeTagName = Node->GetContextualTag().GetTagName();
       
        // Drain the per-cascade snapshot queue. For the immediate-prereq-satisfied case, the queue holds exactly
        // one entry (the cascade that just fired this OnNodeStarted). For the deferred case, the queue may hold
        // multiple - every cascade that arrived during the deferral window stamped its own snapshot. All entries
        // fire here so fan-in convergence patterns route correctly.
        TArray<FQuestObjectiveRuntimeContext> DrainedCascades;
        Swap(DrainedCascades, QuestNode->PendingEntryActivations);
        QuestNode->PendingActivationContext = FQuestObjectiveRuntimeContext{};

        // Defensive synthesis for paths that fire OnNodeStarted without going through ActivateNodeByTag's queue
        // append (e.g., direct Activate calls). Synthesizes one empty cascade so Any-Outcome entries still fire.
        if (DrainedCascades.IsEmpty())
        {
            DrainedCascades.Add(FQuestObjectiveRuntimeContext{});
        }

        // Use the first cascade's params for Any-Outcome entries (these fire ONCE per OnNodeStarted, not per
        // cascade - they're unconditional "Quest started" entries). Matches pre-queue behavior where the first
        // cascade's stamping won via diamond convergence on subsequent calls.
        const FQuestObjectiveRuntimeContext& FirstCascade = DrainedCascades[0];
        TArray<FGameplayTag> AnyOutcomeChain = FirstCascade.IncomingContext.OriginChain;
        if (QuestNode->GetContextualTag().IsValid())
        {
            AnyOutcomeChain.Add(QuestNode->GetContextualTag());
        }

        auto StampWithParams = [this, &QuestNode](const FName& DestTagName,
            const FQuestObjectiveRuntimeContext& Params, const TArray<FGameplayTag>& Chain)
        {
            if (UQuestNodeBase* DestInstance = LoadedNodeInstances.FindRef(DestTagName))
            {
                DestInstance->PendingActivationContext = Params;
                DestInstance->PendingActivationContext.IncomingContext.OriginTag = QuestNode->GetContextualTag();
                DestInstance->PendingActivationContext.IncomingContext.OriginChain = Chain;
            }
        };

        // Always-activate Any-Outcome entries. Fire ONCE per OnNodeStarted, not per cascade.
        for (const FName& StepTag : QuestNode->GetEntryStepTags())
        {
            StampWithParams(StepTag, FirstCascade, AnyOutcomeChain);
            ActivateNodeByTag(StepTag, EQuestActivationProvenance::ChainCascade);
        }

        // Per-cascade outcome-specific routing. Each queued cascade fires its own entry routes - this is the
        // path that fan-in convergence patterns rely on (Q1's Victory and Q2's Defeat both routing into separate
        // inner steps when the Quest's prereq finally satisfies).
        for (const FQuestObjectiveRuntimeContext& CascadeContext : DrainedCascades)
        {
            const FGameplayTag IncomingOutcomeTag = CascadeContext.IncomingOutcomeTag;
            const FName IncomingSourceTag = CascadeContext.IncomingContext.OriginTag.IsValid() ? CascadeContext.IncomingContext.OriginTag.GetTagName() : NAME_None;

            // Record this cascade's per-source entry into the QuestStateSubsystem entry registry. Parallel to
            // the resolution registry pattern from item 2: appends an FQuestEntryArrival to the destination's
            // FQuestEntryRecord and broadcasts FQuestEntryRecordedEvent on the destination's tag channel.
            // Inner-graph Leaf_Entry prereqs subscribe to that event via FPrereqLeafSubscription and re-evaluate.
            if (IncomingOutcomeTag.IsValid() && QuestStateSubsystem && QuestNode->GetContextualTag().IsValid())
            {
                const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
                QuestStateSubsystem->RecordEntry(
                    QuestNode->GetContextualTag(),
                    CascadeContext.IncomingContext.OriginTag,
                    IncomingOutcomeTag,
                    Now,
                    CascadeContext.Provenance,
                    CascadeContext.IncomingContext,
                    IncomingSourceTag,
                    CascadeContext.IncomingContext.OriginatingEventID);
                UE_LOG(LogSimpleQuestActivation, Verbose, TEXT("HandleOnNodeStarted: recorded entry for '%s' source='%s' outcome='%s' provenance=%s path='%s'"),
                    *QuestNode->GetContextualTag().ToString(),
                    *CascadeContext.IncomingContext.OriginTag.ToString(),
                    *IncomingOutcomeTag.ToString(),
                    *UEnum::GetValueAsString(CascadeContext.Provenance),
                    *IncomingSourceTag.ToString());
            }

            // Build chain for this cascade.
            TArray<FGameplayTag> InnerForwardChain = CascadeContext.IncomingContext.OriginChain;
            if (QuestNode->GetContextualTag().IsValid())
            {
                InnerForwardChain.Add(QuestNode->GetContextualTag());
            }

            // Outcome-specific, source-filtered entries.
            if (IncomingOutcomeTag.IsValid())
            {
                if (const FQuestEntryRouteList* RouteList = QuestNode->GetEntryStepTagsByPath().Find(IncomingOutcomeTag.GetTagName()))
                {
                    for (const FQuestEntryDestination& Entry : RouteList->Destinations)
                    {
                        if (Entry.SourceFilter == IncomingSourceTag)
                        {
                            StampWithParams(Entry.DestTag, CascadeContext, InnerForwardChain);
                            ActivateNodeByTag(Entry.DestTag, EQuestActivationProvenance::ChainCascade);
                        }
                    }
                }
            }

            // Any-outcome-from-source entries - bucket keyed by invalid FGameplayTag. Fires when the incoming
            // source matches, regardless of which specific outcome triggered entry.
            if (IncomingSourceTag != NAME_None)
            {
                if (const FQuestEntryRouteList* AnyRouteList = QuestNode->GetEntryStepTagsByPath().Find(NAME_None))
                {
                    for (const FQuestEntryDestination& Entry : AnyRouteList->Destinations)
                    {
                        if (Entry.SourceFilter == IncomingSourceTag)
                        {
                            StampWithParams(Entry.DestTag, CascadeContext, InnerForwardChain);
                            ActivateNodeByTag(Entry.DestTag, EQuestActivationProvenance::ChainCascade);
                        }
                    }
                }
            }
        }
    }
}

void UQuestManagerSubsystem::HandleOnNodeActivationRefused(UQuestNodeBase* Node, FGameplayTag InContextualTag)
{
    // Recorded, not published. A deferral is normal gating rather than a failure, so raising FQuestActivationFailedEvent
    // for it would tell observers something went wrong when nothing did. The record answers "did an activation just reach
    // this node and stop here", which is the question with no other source.
    if (QuestStateSubsystem && InContextualTag.IsValid())
    {
        QuestStateSubsystem->RecordActivationRefusal(InContextualTag, EQuestActivationBlocker::PrereqUnmet, GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0);
    }
}

void UQuestManagerSubsystem::HandleGiveBlockedForRecord(FGameplayTag Channel, const FQuestGiveBlockedEvent& Event)
{
    if (!QuestStateSubsystem || Event.Blockers.Num() == 0 || !Event.QuestTag.IsValid()) return;

    QuestStateSubsystem->RecordActivationRefusal(Event.QuestTag, Event.Blockers[0].Reason, GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0);
}

void UQuestManagerSubsystem::HandleProgressRefusedForRecord(FGameplayTag Channel, const FQuestProgressRefusedEvent& Event)
{
    if (!QuestStateSubsystem || Event.Blockers.Num() == 0 || !Event.QuestTag.IsValid()) return;

    QuestStateSubsystem->RecordActivationRefusal(Event.QuestTag, Event.Blockers[0].Reason, GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0);
}

void UQuestManagerSubsystem::HandleOnNodeForwardActivated(UQuestNodeBase* Node)
{
    if (!Node) return;

    UE_LOG(LogSimpleQuestActivation, Verbose,
        TEXT("HandleOnNodeForwardActivated: '%s' - %d boundary completion(s), %d resolved graph(s), %d next node(s)"),
        *Node->GetContextualTag().ToString(),
        Node->GetBoundaryCompletionsOnForward().Num(),
        Node->GetResolvedGraphsOnForward().Num(),
        Node->GetNextNodesOnForward().Num());
    
    // The utility node's PendingActivationContext was populated by the upstream activation (cascade stamp, or
    // signal-driven self-stamp on UActivationGroupListenerNode). Its OriginatingEventID identifies the gameplay
    // event that drove the upstream cascade - pass it to FireWrapperBoundaryCompletion so the wrapper gate
    // sees the same identity that already-cascade-bearing destinations would. The wholesale
    // PendingActivationContext copy below carries OriginatingEventID onto downstream destinations naturally.
    const FOriginatingEventID& InheritedEventID = Node->PendingActivationContext.IncomingContext.OriginatingEventID;

    // Fire questline-asset resolutions for any Exit/Outcome terminals the utility's Forward output reaches at
    // asset root scope. Done before boundary completions + downstream chaining so the asset's resolution
    // record + bus event land before any cascade off the utility's other forward destinations.
    if (!Node->GetResolvedGraphsOnForward().IsEmpty())
    {
        PublishGraphResolutions(Node->GetResolvedGraphsOnForward(), EQuestResolutionSource::Graph, Node->PendingActivationContext.IncomingContext);
    }
    
    // Fire wrapper boundary completions BEFORE chaining downstream. Wrapper Path facts must exist before any
    // downstream prereq evaluation runs. Routes through the shared FireWrapperBoundaryCompletion helper so
    // the wrapper's full outcome chain fires (including loop-back wires) - symmetric with ChainToNextNodes's
    // FireBoundaryCompletion lambda. Empty when the utility's forward output doesn't cross a wrapper Exit
    // (the common mid-graph utility chaining case).
    for (const FQuestBoundaryCompletion& BC : Node->GetBoundaryCompletionsOnForward())
    {
        UE_LOG(LogSimpleQuestActivation, Verbose,
            TEXT("HandleOnNodeForwardActivated: firing boundary completion '%s' outcome='%s' (from utility '%s')"),
            *BC.WrapperTagName.ToString(),
            *BC.OutcomeTag.ToString(),
            *Node->GetContextualTag().ToString());

        FireWrapperBoundaryCompletion(BC, InheritedEventID, Node->PendingActivationContext.IncomingContext);
    }

    // Thread the source utility node's PendingActivationContext onto each downstream destination so any payload
    // that arrived at the utility (via signal-driven self-stamp on UActivationGroupListenerNode, cascade, or direct
    // upstream stamp) propagates through the forward chain. Mirrors ChainToNextNodes::StampAndActivate. Identity
    // for utility nodes that don't carry payload (SetBlocked / ClearBlocked) - those fields stay zero-init either
    // way so the stamp is a harmless overwrite. OriginatingEventID rides through this wholesale copy.
    for (const FName& Tag : Node->GetNextNodesOnForward())
    {
        if (UQuestNodeBase* DestInstance = LoadedNodeInstances.FindRef(Tag))
        {
            DestInstance->PendingActivationContext = Node->PendingActivationContext;
        }
        ActivateNodeByTag(Tag, EQuestActivationProvenance::ChainCascade);
    }
}

// -------------------------------------------------------------------------------------------------
// Advancement holds
// -------------------------------------------------------------------------------------------------


// How often the abandonment check runs while any hold is active. NOT the threshold - only the resolution at which the
// threshold gets noticed, and cheap enough at this cadence to not be worth configuring.
static constexpr float AbandonedHoldCheckIntervalSeconds = 5.f;

FQuestAdvancementHold UQuestManagerSubsystem::HoldQuestAdvancement(FGameplayTag QuestTag, FName Reason, bool bHoldDeactivation)
{
    FQuestAdvancementHold Handle;
    if (!QuestTag.IsValid())
    {
        UE_LOG(LogSimpleQuestActivation, Warning, TEXT("HoldQuestAdvancement: refused - invalid QuestTag (reason '%s')"), *Reason.ToString());
        return Handle;
    }

    Handle.Id = NextHoldId++;
    FQuestHoldRecord& Record = ActiveHolds.Add(Handle.Id);
    Record.QuestTag          = QuestTag;
    Record.Reason            = Reason;
    Record.bHoldDeactivation = bHoldDeactivation;
    Record.PlacedAtSeconds   = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;

    // Started here and nowhere else; an idle game pays nothing for this net because the timer does not exist until
    // something is actually held.
    if (!AbandonedHoldTimer.IsValid())
    {
        if (UWorld* World = GetWorld())
        {
            World->GetTimerManager().SetTimer(AbandonedHoldTimer, this, &UQuestManagerSubsystem::CheckForAbandonedHolds, AbandonedHoldCheckIntervalSeconds, true);
        }
    }

    AddStateFactAcrossPerspectives(QuestTag, EQuestStateLeaf::Held);

    UE_LOG(LogSimpleQuestActivation, Log, TEXT("HoldQuestAdvancement: '%s' (id=%d, reason='%s', deactivation=%s) - %d hold(s) active"),
        *QuestTag.ToString(), Handle.Id, *Reason.ToString(), bHoldDeactivation ? TEXT("held") : TEXT("allowed"), ActiveHolds.Num());

    return Handle;
}

void UQuestManagerSubsystem::ReleaseQuestAdvancement(const FQuestAdvancementHold& Hold)
{
    if (!Hold.IsValid()) return;

    FQuestHoldRecord Removed;
    if (!ActiveHolds.RemoveAndCopyValue(Hold.Id, Removed))
    {
        // Releasing twice is not an error. A holder that cannot tell whether it already released should be able to
        // just call this, rather than tracking state the manager already tracks.
        UE_LOG(LogSimpleQuestActivation, Verbose, TEXT("ReleaseQuestAdvancement: id=%d is not active - already released"), Hold.Id);
        return;
    }

    RefreshHeldFact(Removed.QuestTag);

    UE_LOG(LogSimpleQuestActivation, Log, TEXT("ReleaseQuestAdvancement: '%s' (id=%d, reason='%s') - %d hold(s) remain, %d parked"),
        *Removed.QuestTag.ToString(), Hold.Id, *Removed.Reason.ToString(), ActiveHolds.Num(), ParkedActivations.Num());

    ReplayParkedActivations();
    RetryDeferredActivations();
}

void UQuestManagerSubsystem::CheckForAbandonedHolds()
{
    // *** STOPS ITSELF RATHER THAN BEING STOPPED. *** Holds leave ActiveHolds in three places - a single release, the
    // bulk drop a save capture performs (ReleaseAllQuestAdvancementHolds), and ClearHoldsForEndedQuest - and a stop
    // condition spread across three sites is one a fourth site added later will forget, leaking a repeating timer.
    if (ActiveHolds.IsEmpty())
    {
        if (UWorld* World = GetWorld())
        {
            World->GetTimerManager().ClearTimer(AbandonedHoldTimer);
        }
        AbandonedHoldTimer.Invalidate();
        return;
    }

    if (const UWorld* World = GetWorld())
    {
        WarnOnHoldsOlderThan(World->GetTimeSeconds());
    }
}

void UQuestManagerSubsystem::WarnOnHoldsOlderThan(double Now)
{
    const float Threshold = GetDefault<USimpleQuestSettings>()->AbandonedHoldWarningSeconds;
    if (Threshold <= 0.f) return;

    for (TPair<int32, FQuestHoldRecord>& Pair : ActiveHolds)
    {
        FQuestHoldRecord& Record = Pair.Value;
        if (Record.bWarned) continue;

        const double Elapsed = Now - Record.PlacedAtSeconds;
        if (Elapsed < Threshold) continue;
        
        // Once per hold. A hold this old is stuck for the rest of the session, and repeating the warning every few
        // seconds would bury the thing it exists to surface.
        Record.bWarned = true;
        UE_LOG(LogSimpleQuestActivation, Warning,
            TEXT("Advancement hold '%s' on '%s' has been active for %.0f seconds and is still holding. Whatever placed it "
                 "has most likely gone away without releasing - the questline will not advance until something does. Holds "
                 "are never released automatically, because that would hide this rather than report it."),
            *Record.Reason.ToString(), *Record.QuestTag.ToString(), Elapsed);
    }
}

bool UQuestManagerSubsystem::IsQuestAdvancementHeld(FGameplayTag QuestTag) const
{
    if (!QuestTag.IsValid() || ActiveHolds.Num() == 0) return false;

    const UQuestNodeBase* Instance = LoadedNodeInstances.FindRef(QuestTag.GetTagName());
    for (const TPair<int32, FQuestHoldRecord>& Pair : ActiveHolds)
    {
        if (NodeMatchesHoldTag(Instance, QuestTag.GetTagName(), Pair.Value.QuestTag)) return true;
    }
    return false;
}

TArray<FName> UQuestManagerSubsystem::GetActiveHoldReasons(FGameplayTag QuestTag) const
{
    TArray<FName> Reasons;
    if (!QuestTag.IsValid()) return Reasons;

    const UQuestNodeBase* Instance = LoadedNodeInstances.FindRef(QuestTag.GetTagName());
    for (const TPair<int32, FQuestHoldRecord>& Pair : ActiveHolds)
    {
        if (NodeMatchesHoldTag(Instance, QuestTag.GetTagName(), Pair.Value.QuestTag))
        {
            Reasons.AddUnique(Pair.Value.Reason);
        }
    }
    return Reasons;
}

int32 UQuestManagerSubsystem::ReleaseAllQuestAdvancementHolds()
{
    if (ActiveHolds.Num() == 0 && ParkedActivations.Num() == 0) return 0;

    const int32 Dropped = ActiveHolds.Num();

    // Collect the tags before emptying, so the Held facts can be cleared afterwards. Clearing them first would leave
    // a window where the registry disagrees with the facts.
    TArray<FGameplayTag> HeldTags;
    for (const TPair<int32, FQuestHoldRecord>& Pair : ActiveHolds) HeldTags.AddUnique(Pair.Value.QuestTag);

    ActiveHolds.Reset();
    for (const FGameplayTag& Tag : HeldTags) RemoveStateFactAcrossPerspectives(Tag, EQuestStateLeaf::Held);

    UE_LOG(LogSimpleQuestActivation, Log, TEXT("ReleaseAllQuestAdvancementHolds: dropped %d hold(s), replaying %d parked activation(s)"),
        Dropped, ParkedActivations.Num());

    ReplayParkedActivations();
    return Dropped;
}

bool UQuestManagerSubsystem::NodeMatchesHoldTag(const UQuestNodeBase* Instance, FName NodeTagName, FGameplayTag HoldTag) const
{
    if (!HoldTag.IsValid()) return false;

    // The lookup spelling first. For a tag that is not a node at all - a questline's identity tag, a container - it is
    // the only spelling there is, and MatchesTag gives exact-or-descendant against the hold.
    const FGameplayTag LookupTag = UGameplayTagsManager::Get().RequestGameplayTag(NodeTagName, false);
    if (LookupTag.IsValid() && LookupTag.MatchesTag(HoldTag)) return true;

    if (!Instance) return false;

    if (Instance->GetContextualTag().IsValid() && Instance->GetContextualTag().MatchesTag(HoldTag)) return true;

    // Every other perspective the node answers to. A hold authored against a questline's standalone spelling has to
    // reach the same node running under a parent, or the hold silently does nothing and the cascade that failed to
    // pause is undebuggable. Same reasoning that made DeriveContainerLive write across perspectives.
    for (const FGameplayTag& AliasTag : Instance->GetAssetScopedAliasTags())
    {
        if (AliasTag.IsValid() && AliasTag.MatchesTag(HoldTag)) return true;
    }
    return false;
}

bool UQuestManagerSubsystem::ShouldHoldActivation(const UQuestNodeBase* Instance, FName NodeTagName, EQuestActivationProvenance Provenance, FName IncomingSourceTag) const
{
    if (ActiveHolds.Num() == 0) return false;

    // Cascade provenances only. A give, an external activation request, or an initial entry is a deliberate act by
    // something outside the chain, and pacing the chain is not a reason to refuse it.
    const bool bIsDeactivation = (Provenance == EQuestActivationProvenance::DeactivationCascade);
    if (Provenance != EQuestActivationProvenance::ChainCascade && !bIsDeactivation) return false;

    // A hold on X pauses anything going INTO X and anything coming OUT OF X. The second direction is the one the
    // feature exists for: game code pacing a completion knows what just finished, not what follows it, so requiring
    // it to name the destination would mean encoding the graph's topology at every call site - and re-encoding it
    // every time someone rewires the graph.
    const UQuestNodeBase* SourceInstance = LoadedNodeInstances.FindRef(IncomingSourceTag);
    for (const TPair<int32, FQuestHoldRecord>& Pair : ActiveHolds)
    {
        if (bIsDeactivation && !Pair.Value.bHoldDeactivation) continue;
        // SOURCE ONLY. A hold names the node whose downstream flow is paused - it does not pause the node itself.
        // Matching the destination as well is what made holding a chapter freeze the step the player was standing in
        // front of: correct by the rule, wrong by every intuition, and the wrong feature entirely. Refusing a player
        // is what Block is for.
        if (!IncomingSourceTag.IsNone() && NodeMatchesHoldTag(SourceInstance, IncomingSourceTag, Pair.Value.QuestTag)) return true;
    }
    return false;
}

void UQuestManagerSubsystem::RefreshHeldFact(FGameplayTag QuestTag)
{
    if (!QuestTag.IsValid()) return;

    // Another hold may still name this exact tag. The fact is per-tag, not per-hold, so it survives until the last one.
    for (const TPair<int32, FQuestHoldRecord>& Pair : ActiveHolds)
    {
        if (Pair.Value.QuestTag == QuestTag) return;
    }
    RemoveStateFactAcrossPerspectives(QuestTag, EQuestStateLeaf::Held);
}

void UQuestManagerSubsystem::ReplayParkedActivations()
{
    if (bReplayingParkedActivations || ParkedActivations.Num() == 0) return;

    TGuardValue<bool> Guard(bReplayingParkedActivations, true);

    // Take the queue rather than iterating it. Anything still held by a REMAINING hold re-parks itself when it
    // re-enters ActivateNodeByTag, which preserves arrival order for the entries that stay and avoids a second
    // matching pass here. Entries added during the replay wait for the next release - the guard stops a released
    // activation that completes and releases another hold from recursing into this function mid-drain.
    TArray<FQuestParkedActivation> Pending = MoveTemp(ParkedActivations);
    ParkedActivations.Reset();

    for (const FQuestParkedActivation& Entry : Pending)
    {
        ActivateNodeByTag(Entry.NodeTagName, Entry.Provenance, Entry.IncomingOutcomeTag, Entry.IncomingSourceTag,
            Entry.bBypassGiverGate, Entry.bBypassPrerequisites);
    }
}

void UQuestManagerSubsystem::RetryDeferredActivations()
{
    // Nodes waiting on a prerequisite are NOT parked - they were never activated in the first place, so there is no
    // call to replay. They are sitting in their own deferral, subscribed to the facts they need. A hold makes them
    // treat "satisfied" as "not yet", so when it clears they have to be told to look again.
    //
    // DELIBERATELY RE-EVALUATES RATHER THAN REMEMBERING. A waiting node is nothing but conditions, and those can
    // change while a hold is in force - the prerequisite that satisfied during the pause may not still be satisfied
    // when it lifts. Asking again is what a waiting node already does; the hold only delayed when it asks.
    int32 Retried = 0;
    for (const TPair<FName, TObjectPtr<UQuestNodeBase>>& Pair : LoadedNodeInstances)
    {
        UQuestNodeBase* Node = Pair.Value;
        if (Node && Node->DeferredContextualTag.IsValid())
        {
            Node->TryActivateDeferred();
            ++Retried;
        }
    }

    if (Retried > 0)
    {
        UE_LOG(LogSimpleQuestActivation, Log, TEXT("RetryDeferredActivations: re-evaluated %d deferred node(s) after a hold change"), Retried);
    }
}

void UQuestManagerSubsystem::ClearHoldsForEndedQuest(FGameplayTag QuestTag)
{
    if (!QuestTag.IsValid() || ActiveHolds.Num() == 0) return;

    TArray<int32> Doomed;
    for (const TPair<int32, FQuestHoldRecord>& Pair : ActiveHolds)
    {
        if (Pair.Value.QuestTag == QuestTag) Doomed.Add(Pair.Key);
    }
    if (Doomed.Num() == 0) return;

    for (const int32 Id : Doomed)
    {
        const FQuestHoldRecord Record = ActiveHolds.FindChecked(Id);
        UE_LOG(LogSimpleQuestActivation, Warning,
            TEXT("ClearHoldsForEndedQuest: '%s' went away while hold id=%d (reason='%s') was still active - dropping it. ")
            TEXT("Game code placed a hold and never released it before the quest it names was torn down."),
            *QuestTag.ToString(), Id, *Record.Reason.ToString());
        ActiveHolds.Remove(Id);
    }

    RefreshHeldFact(QuestTag);
    ReplayParkedActivations();
}

void UQuestManagerSubsystem::ActivateNodeByTag(FName NodeTagName, EQuestActivationProvenance Provenance, FGameplayTag IncomingOutcomeTag, FName IncomingSourceTag, bool bBypassGiverGate, bool bBypassPrerequisites)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(UQuestManagerSubsystem_ActivateNodeByTag);

    TObjectPtr<UQuestNodeBase>* InstancePtr = LoadedNodeInstances.Find(NodeTagName);
    if (!InstancePtr || !*InstancePtr)
    {
        UE_LOG(LogSimpleQuestActivation, Warning, TEXT("UQuestManagerSubsystem::ActivateNodeByTag : no instance found for tag name '%s'"), *NodeTagName.ToString());
        PublishUnknownQuestFailure(QuestSignalSubsystem, LoadedNodeInstances, NodeTagName, FQuestEventPayload());
        return;
    }

    UQuestNodeBase* Instance = *InstancePtr;

    // Advancement hold, checked BEFORE anything is written to the destination node. Parking here means the node was
    // never touched, so a replayed activation is indistinguishable from one that was never held - no provenance to
    // re-stamp, no one-shot bypass flag to un-set, no half-configured instance to reason about.
    if (ShouldHoldActivation(Instance, NodeTagName, Provenance, IncomingSourceTag))
    {
        FQuestParkedActivation& Parked = ParkedActivations.AddDefaulted_GetRef();
        Parked.NodeTagName          = NodeTagName;
        Parked.Provenance           = Provenance;
        Parked.IncomingOutcomeTag   = IncomingOutcomeTag;
        Parked.IncomingSourceTag    = IncomingSourceTag;
        Parked.bBypassGiverGate     = bBypassGiverGate;
        Parked.bBypassPrerequisites = bBypassPrerequisites;

        UE_LOG(LogSimpleQuestActivation, Log, TEXT("ActivateNodeByTag: '%s' HELD - parked, queue depth %d"),
            *NodeTagName.ToString(), ParkedActivations.Num());
        return;
    }

    // Stamp activation provenance on the destination's PendingActivationContext. ActivateInternal merges this into
    // ReceivedActivationContext, and HandleOnNodeStarted's Step-side RecordEntry reads the snapshot's Provenance into
    // FQuestEntryArrival. Stamped after lookup, before the rest of ActivateNodeByTag's flow touches PendingActivation-
    // Params, so this value rides through the merge regardless of whether the caller pre-stamped other fields on the struct.
    Instance->PendingActivationContext.Provenance = Provenance;
    
    // One-shot prereq-bypass directive for this activation, consumed in UQuestNodeBase::Activate. Re-stamped on every
    // ActivateNodeByTag call (organic cascade activations pass false), so a true value can't leak into a later call.
    Instance->bBypassPrerequisitesOnce = bBypassPrerequisites;

    const FGameplayTag NodeTag = UGameplayTagsManager::Get().RequestGameplayTag(NodeTagName, false);

    // Route the diamond + giver-gate + Block-gate decision through the centralized policy evaluator. ActivateNodeByTag
    // becomes a switch over the decision; existing handlers (logging, SetQuestPendingGiver + event publishes + watch
    // registration, the Deactivated clear, the cascade-origin / IncomingOutcomeTag / PendingEntryActivations side effects,
    // the Activate call) stay verbatim, just relocated under their decision case. See FQuestActivationGuard for the
    // policy logic and EQuestActivationGuardDecision for the case enumeration.

    // Multi-tag aware giver-presence check. A giver actor authored against a step's standalone-perspective tag (the
    // natural authoring form) registers under that tag in RegisteredGiverQuestTags. The same step can be reached
    // via three different tag forms at activation time:
    //   1. NodeTag - the FName key the caller used to look up the instance. May be the instance's ContextualTag
    //      OR an alias key (since LoadedNodeInstances is populated under both the canonical key and each alias).
    //   2. Instance->GetContextualTag() - the canonical form of the instance itself, which may differ from
    //      NodeTag when the lookup resolved via an alias key.
    //   3. Instance->GetAssetScopedAliasTags() - every other perspective the instance carries.
    // The giver could have been authored against any of these, depending on which asset's content the designer
    // was working in. All three need to be checked or inlined-step givers silently miss the gate. Pre-AuthoredGuid-
    // deduplicate this loop got away with checking just (1) + (3) because (1) matched the instance's ContextualTag in
    // practice; post-deduplication the canonical instance may sit under an alias-key lookup and (1) ≠ ContextualTag.
    bool bHasRegisteredGiver = NodeTag.IsValid() && RegisteredGiverQuestTags.Contains(NodeTag);
    if (!bHasRegisteredGiver)
    {
        const FGameplayTag ContextualTag = Instance->GetContextualTag();
        if (ContextualTag.IsValid() && RegisteredGiverQuestTags.Contains(ContextualTag))
        {
            bHasRegisteredGiver = true;
        }
    }
    if (!bHasRegisteredGiver)
    {
        for (const FGameplayTag& AliasTag : Instance->GetAssetScopedAliasTags())
        {
            if (AliasTag.IsValid() && RegisteredGiverQuestTags.Contains(AliasTag))
            {
                bHasRegisteredGiver = true;
                break;
            }
        }
    }
    const EQuestActivationGuardDecision Decision = FQuestActivationGuard::Evaluate(WorldState, Instance, NodeTag, IncomingOutcomeTag, bBypassGiverGate, bHasRegisteredGiver);

    // Step diamond refusal - early out BEFORE the Deactivated clear (matches original behavior where Step refusal
    // returned before reaching the clear). Steps own their state directly; re-firing would corrupt lifecycle invariants
    // and double-publish FQuestStartedEvent.
    if (Decision == EQuestActivationGuardDecision::RefuseStepAlreadyLive)
    {
        UE_LOG(LogSimpleQuestActivation, Verbose, TEXT("ActivateNodeByTag: '%s' skipped (already live)"),
            *NodeTagName.ToString());
        
        if (QuestSignalSubsystem)
        {
            FQuestEventPayload Context = AssembleEventContext(Instance, FQuestObjectiveTriggerContext());
            FQuestPublish::OnAllNodeTags(QuestSignalSubsystem, Instance, FQuestActivationFailedEvent(NodeTag, NodeTagName, EQuestActivationBlocker::AlreadyLive, Context));
            if (QuestStateSubsystem)
            {
                QuestStateSubsystem->RecordActivationRefusal(NodeTag, EQuestActivationBlocker::AlreadyLive, GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0);
            }
        }
        return;
    }
    if (Decision == EQuestActivationGuardDecision::RefuseStepAlreadyPendingGiver)
    {
        UE_LOG(LogSimpleQuestActivation, Verbose, TEXT("ActivateNodeByTag: '%s' skipped (already pending giver)"),
            *NodeTagName.ToString());

        if (QuestSignalSubsystem)
        {
            FQuestEventPayload Context = AssembleEventContext(Instance, FQuestObjectiveTriggerContext());
            FQuestPublish::OnAllNodeTags(QuestSignalSubsystem, Instance, FQuestActivationFailedEvent(NodeTag, NodeTagName, EQuestActivationBlocker::AlreadyPendingGiver, Context));
            if (QuestStateSubsystem)
            {
                QuestStateSubsystem->RecordActivationRefusal(NodeTag, EQuestActivationBlocker::AlreadyPendingGiver, GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0);
            }
        }
        return;
    }

    // Clear Deactivated for any non-Step-refusal path. Matches the original behavior where ANY Activate-input pulse
    // clears Deactivated, even if downstream guards (Block) ultimately refuse the activation. A deactivated node is
    // allowed to re-enter via its Activate input. Multi-perspective remove keeps the alias-perspective Deactivated
    // facts in sync with the canonical clear - without this, a re-entered node could remain visibly Deactivated
    // under aliases while its canonical reads as active.
    if (NodeTag.IsValid() && WorldState)
    {
        RemoveStateFactAcrossPerspectives(NodeTag, EQuestStateLeaf::Deactivated);
    }

    // Resettable-replay reset. A resettable-scoped node that (re-)activates after having completed - or via an
    // explicit prerequisite bypass - begins a fresh run, so clear its per-run path mirror(s) before it runs so
    // gates wired from it re-gate honestly. The append-only resolution registry and the Completed anchor are NEVER
    // touched; only the clearable projection (ClearFact, count-agnostic). Guards: the activation must proceed (a
    // Block-refused activation doesn't re-run), and the node must not already be Live (a no-op mid-run re-entry must
    // not wipe in-flight mirrors). Descendants need no explicit walk - the inward activation cascade re-activates
    // each one, which resets itself here the same way; that also correctly leaves the mirrors of any branch a replay
    // doesn't re-enter. Mirrors to clear come from the node's own resolution history (the registry knows the paths).
    if (Decision != EQuestActivationGuardDecision::RefuseBlocked && Instance->IsResettableReplay() && NodeTag.IsValid() && WorldState)
    {
        const FGameplayTag CompletedFact = FQuestTagComposer::ResolveStateFactTag(NodeTag, EQuestStateLeaf::Completed);
        const FGameplayTag LiveFact = FQuestTagComposer::ResolveStateFactTag(NodeTag, EQuestStateLeaf::Live);
        const bool bWasCompleted = CompletedFact.IsValid() && WorldState->HasFact(CompletedFact);
        const bool bCurrentlyLive = LiveFact.IsValid() && WorldState->HasFact(LiveFact);
        if (!bCurrentlyLive && (bWasCompleted || bBypassPrerequisites))
        {
            ResetQuestRunState(NodeTag);
            UE_LOG(LogSimpleQuestActivation, Verbose, TEXT("[Resettable] reset path mirrors for '%s' (completed=%d bypass=%d)"),
                *NodeTag.ToString(),
                bWasCompleted,
                bBypassPrerequisites);
        }
    }

    switch (Decision)
    {
    case EQuestActivationGuardDecision::RefuseBlocked:
        // Block gate - orthogonal to lifecycle. Block intentionally allows the giver-gate fire (so the giver stays
        // visible and the player can attempt to interact) but refuses the actual SetQuestLive transition. This mirrors
        // HandleGiveQuestEvent's blocker check at the give step: gives on Blocked quests are refused with FQuestGive-
        // BlockedEvent, and direct/cascade activations on Blocked quests are refused here. Together these make Block a
        // pure re-initiation gate that doesn't disable targets/givers (those are SetQuestDeactivated's job).
        UE_LOG(LogSimpleQuestActivation, Verbose, TEXT("ActivateNodeByTag: '%s' skipped - Blocked (re-initiation refused, giver/targets untouched)"),
            *NodeTagName.ToString());

        if (QuestSignalSubsystem)
        {
            FQuestEventPayload Context = AssembleEventContext(Instance, FQuestObjectiveTriggerContext());
            FQuestPublish::OnAllNodeTags(QuestSignalSubsystem, Instance, FQuestActivationFailedEvent(NodeTag, NodeTagName, EQuestActivationBlocker::Blocked, Context));
            if (QuestStateSubsystem)
            {
                QuestStateSubsystem->RecordActivationRefusal(NodeTag, EQuestActivationBlocker::Blocked, GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0);
            }
        }
        return;

    case EQuestActivationGuardDecision::GiverGateFire:
    {
        // Giver gate - sets PendingGiver state, publishes FQuestActivatedEvent (always) plus FQuestEnabledEvent if
        // prereqs are already satisfied, and registers an EnablementWatch when prereqs are non-Always so the gate can
        // re-publish Enabled when leaves satisfy mid-PendingGiver. Block is intentionally NOT pre-checked - Block-on-
        // giver-gated quests still publishes the activation events so the giver stays visible/interactive.
        //
        // State writes and watch registrations route the cascade's NodeTag through ResolveToCanonicalTag so every
        // call below targets the canonical ContextualTag the state subsystem's queries alias-walk to. Under
        // AuthoredGuid-dedup the cascade's NodeTag may be an alias-key form; writing under that form leaves the
        // canonical fact missing and QueryQuestActivationBlockers returns NotPendingGiver despite the gate having
        // fired. Matches the canonical-resolution pattern used by the request-side BP handlers (HandleResolveRequest,
        // HandleNodeDeactivationRequest, etc.) and the Node->GetContextualTag() convention used by SetQuestLive /
        // SetQuestResolved at their respective call sites.
        const FGameplayTag CanonicalTag = ResolveToCanonicalTag(NodeTag);
        const FName CanonicalTagName = CanonicalTag.GetTagName();

        Instance->bWasGiverGated = true;
        SetQuestPendingGiver(CanonicalTag);

        if (QuestSignalSubsystem)
        {
            FQuestEventPayload Context = AssembleEventContext(Instance, FQuestObjectiveTriggerContext());
            const FQuestPrereqStatus PrereqStatus = Instance->PrerequisiteExpression.EvaluateWithLeafStatus(WorldState, QuestStateSubsystem);

            // Push the prereq status to the state subsystem before publishing events. The state subsystem's
            // QueryQuestActivationBlockers reads this cache; subsequent designer queries (or our own
            // HandleGiveQuestEvent acceptance gate) will see the correct PrereqUnmet status.
            if (UQuestStateSubsystem* StateSubsystem = GetGameInstance() ? GetGameInstance()->GetSubsystem<UQuestStateSubsystem>() : nullptr)
            {
                StateSubsystem->UpdateQuestPrereqStatus(CanonicalTag, PrereqStatus);
            }

            FQuestPublish::OnAllNodeTags(QuestSignalSubsystem, Instance, FQuestActivatedEvent(CanonicalTag, Context, PrereqStatus));

            if (PrereqStatus.bSatisfied)
            {
                FQuestPublish::OnAllNodeTags(QuestSignalSubsystem, Instance, FQuestEnabledEvent(CanonicalTag, Context));
            }

            if (!Instance->PrerequisiteExpression.IsAlways())
            {
                RegisterEnablementWatch(CanonicalTag, CanonicalTagName, Instance->PrerequisiteExpression, PrereqStatus.bSatisfied);
            }

            UE_LOG(LogSimpleQuestActivation, Log, TEXT("ActivateNodeByTag: '%s' (canonical '%s') gated by giver - Activated published, prereqs %s"),
                *NodeTagName.ToString(),
                *CanonicalTagName.ToString(),
                PrereqStatus.bSatisfied ? TEXT("satisfied (Enabled fired)") : TEXT("unmet (watching for satisfy)"));
        }
        return;
    }

    case EQuestActivationGuardDecision::ContainerReentry:
        // Container reentry - containers don't directly write the Live fact; their Live state derives from inner Step
        // state. Re-activating while already Live falls through to the full activation flow so HandleOnNodeStarted's
        // container branch processes Any-Outcome and per-path entries, records entry to UQuestStateSubsystem, and re-
        // publishes FQuestStartedEvent. Loop-back wires (own outer outcome → own Activate) and external fan-in both
        // route through here equivalently. Inner Step diamond guards handle idempotency for already-Live Steps.
        UE_LOG(LogSimpleQuestActivation, Verbose, TEXT("ActivateNodeByTag: '%s' re-activating container while %s"),
            *NodeTagName.ToString(),
            FQuestLifecycleQuery::IsLive(WorldState, NodeTag) ? TEXT("live") : TEXT("pending giver"));
        break;

    case EQuestActivationGuardDecision::GiverGateSkipPathAware:
        // Path-aware giver-gate skip - for containers, all Steps reachable from the entered Activate pin (compile-time
        // populated by ComputeContainerReachability into UQuest::ReachableStepsByActivatePin) are already Live. There's
        // no work for the giver to enable, so skip the gate and fall through to normal activation. Covers loop-back
        // wires, fan-in re-entry, and any case where the entered path's targets are already running - preventing the
        // giver from spuriously re-firing each iteration.
        UE_LOG(LogSimpleQuestActivation, Verbose, TEXT("ActivateNodeByTag: '%s' giver-gate skipped - all reachable Steps from pin '%s' already Live"),
            *NodeTagName.ToString(),
            IncomingOutcomeTag.IsValid() ? *IncomingOutcomeTag.GetTagName().ToString() : TEXT("AnyOutcome"));
        break;

    case EQuestActivationGuardDecision::Proceed:
        // No diamond hit, no giver gate, not blocked - normal forward activation.
        break;

    case EQuestActivationGuardDecision::RefuseStepAlreadyLive:
    case EQuestActivationGuardDecision::RefuseStepAlreadyPendingGiver:
        // Unreachable - handled by the early-return block above. Listed explicitly so future enum additions force a
        // compiler -Wswitch warning if someone forgets to handle a new decision case.
        return;
    }

    // Cascade origin fallback: ChainToNextNodes pre-stamps OriginTag and OriginChain for every cascade destination, so
    // this block is effectively a no-op on the normal cascade path. Retained as a safety net for any direct caller that
    // passes IncomingSourceTag without pre-stamping. Guard on empty OriginChain so the chain propagation built by
    // ChainToNextNodes isn't stomped with a double-append.
    if (IncomingSourceTag != NAME_None)
    {
        if (Instance->PendingActivationContext.IncomingContext.OriginChain.Num() == 0)
        {
            const FGameplayTag SourceTag = UGameplayTagsManager::Get().RequestGameplayTag(IncomingSourceTag, false);
            if (SourceTag.IsValid())
            {
                Instance->PendingActivationContext.IncomingContext.OriginTag = SourceTag;
                Instance->PendingActivationContext.IncomingContext.OriginChain.Add(SourceTag);
            }
        }
    }

    // Stash the incoming outcome on the instance so HandleOnNodeStarted's UQuest branch can route inner entries
    // post-prereq-gate. UQuest's inner-entry activation used to run inline below this Activate call (pre-fix),
    // bypassing UQuestNodeBase::Activate's deferred-prereq path. Routing it through HandleOnNodeStarted ensures
    // inner entries activate only after ActivateInternal actually fires - synchronously when prereqs are satisfied
    // immediately, or later via TryActivateDeferred when a leaf fact arrives.
    Instance->PendingActivationContext.IncomingOutcomeTag = IncomingOutcomeTag;

    // For UQuest containers, snapshot this cascade's params into the per-cascade queue. HandleOnNodeStarted drains the
    // queue and fires entry routes for each cascade - necessary so fan-in convergence patterns (multiple upstream
    // outcomes converging at a single deferred Quest) all route correctly when the prereq satisfies. Without this
    // snapshot, only the most-recently-stamped IncomingOutcomeTag would survive, dropping earlier cascades.
    if (UQuest* QuestNode = Cast<UQuest>(Instance))
    {
        QuestNode->PendingEntryActivations.Add(QuestNode->PendingActivationContext);
    }

    Instance->Activate(NodeTag);

    UE_LOG(LogSimpleQuestActivation, Log, TEXT("ActivateNodeByTag: '%s' activated (source '%s', outcome '%s')"),
        *NodeTagName.ToString(),
        *IncomingSourceTag.ToString(),
        *IncomingOutcomeTag.ToString());
}

void UQuestManagerSubsystem::LoadCompiledDisplayIni() const
{
    UQuestStateSubsystem* StateSubsystem = GetGameInstance() ? GetGameInstance()->GetSubsystem<UQuestStateSubsystem>() : nullptr;
    if (!StateSubsystem) return;

    // Path mirrors FSimpleQuestEditor::GetCompiledDisplayIniPath - worth factoring to a shared constant so write + read can't drift.
    const FString IniPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectConfigDir() / TEXT("SimpleQuest/SimpleQuestCompiledDisplay.ini"));
    FString Content;
    if (!FFileHelper::LoadFileToString(Content, *IniPath))
    {
        UE_LOG(LogSimpleQuestActivation, Log, TEXT("LoadCompiledDisplayIni: none at %s (first run / not yet compiled)"), *IniPath);
        return;
    }

    int32 Registered = 0, Loaded = 0;
    TArray<FString> Lines;
    Content.ParseIntoArrayLines(Lines);
    for (const FString& Line : Lines)
    {
        if (Line.IsEmpty() || Line[0] == TEXT(';') || Line[0] == TEXT('[')) continue;   // comment / section header

        int32 EqualsIdx;
        if (!Line.FindChar(TEXT('='), EqualsIdx)) continue;
        const FGameplayTag Tag = UGameplayTagsManager::Get().RequestGameplayTag(FName(*Line.Left(EqualsIdx)), false);
        if (!Tag.IsValid()) continue;

        // ReadFromBuffer consumes one whole FText literal (quoted or NSLOCTEXT - even if its source contains '|'),
        // returning the cursor just past it. Fields are pipe-separated; the remainder is the DisplayData soft path.
        const TCHAR* Cursor = *Line + EqualsIdx + 1;
        FText DisplayName, Description;
        Cursor = FTextStringHelper::ReadFromBuffer(Cursor, DisplayName, nullptr, /*PackageNamespace*/ nullptr, /*bRequiresQuotes*/ true);
        if (!Cursor || *Cursor != TEXT('|')) continue;
        Cursor = FTextStringHelper::ReadFromBuffer(Cursor + 1, Description, nullptr, nullptr, /*bRequiresQuotes*/ true);
        if (!Cursor || *Cursor != TEXT('|')) continue;
        const FString DataPath(Cursor + 1);   // remainder (may be empty)

        UQuestDisplayData* DisplayData = nullptr;
        if (!DataPath.IsEmpty())
        {
            DisplayData = Cast<UQuestDisplayData>(FSoftObjectPath(DataPath).TryLoad());   // eager sync; assets are small index leaves
            if (DisplayData) ++Loaded;
        }

        StateSubsystem->RegisterQuestTag(Tag);
        StateSubsystem->RegisterDisplayData(Tag, DisplayName, Description, DisplayData);
        ++Registered;
    }

    UE_LOG(LogSimpleQuestActivation, Log, TEXT("LoadCompiledDisplayIni: %d record(s), %d DisplayData asset(s) from %s"), Registered, Loaded, *IniPath);
}

void UQuestManagerSubsystem::ChainToNextNodes(UQuestNodeBase* Node, FGameplayTag OutcomeTag, FName PathIdentity, const FOriginatingEventID& OriginatingEventID, const FQuestObjectiveActivationContext& InheritedForward)
{
    if (!Node) return;

    TRACE_CPUPROFILER_EVENT_SCOPE(UQuestManagerSubsystem_ChainToNextNodes);

    // Cycle guard: refuses recursive re-entry on the same node tag within a single synchronous chain.
    // Required because FireBoundaryCompletion now routes wrapper resolutions back through ChainToNextNodes
    // so wrapper outcome wires actually fire (closing the gap that broke outer-Quest-loops-on-itself topologies).
    // Without this guard, a degenerate authoring path - e.g., ActivationGroup wired entry→exit with no gating
    // step between, then the exit loops the parent wrapper - would stack-overflow because every iteration
    // synchronously re-enters the wrapper. Legitimate nested-wrapper recursion always climbs to outer tags
    // (Step → wrapper → grandparent), never revisits an in-flight tag, so this guard has no false positives.
    const FName NodeTagName = Node->GetContextualTag().GetTagName();
    if (ChainRecursionTags.Contains(NodeTagName))
    {
        UE_LOG(LogSimpleQuestActivation, Error,
            TEXT("ChainToNextNodes: synchronous cycle detected on '%s' outcome='%s' - aborting recursive re-entry. ")
            TEXT("This means a single chain iteration completed without yielding to an external trigger and looped ")
            TEXT("back to the same node. Common causes: (a) ActivationGroup wired entry→exit with no gating step ")
            TEXT("between, then the exit loops the parent's Activate; (b) an Objective that calls CompleteObjective ")
            TEXT("synchronously during initialization on a Step that loops on itself; (c) any equivalent topology ")
            TEXT("where one loop iteration runs to completion within a single call stack. Authoring fix: ensure ")
            TEXT("each iteration includes at least one node that waits for an external trigger (player input, ")
            TEXT("world event, timer) so the synchronous call stack breaks between iterations."),
            *NodeTagName.ToString(), *OutcomeTag.ToString());
        return;
    }
    ChainRecursionTags.Add(NodeTagName);
    ON_SCOPE_EXIT { ChainRecursionTags.Remove(NodeTagName); };

    // Auto-derive PathIdentity from OutcomeTag when caller passes NAME_None. Preserves back-compat for any
    // direct C++ caller that hasn't been updated to thread PathIdentity through. Static K2 placements always
    // produce a PathIdentity matching OutcomeTag.GetTagName(), so this path is a no-op for them.
    const FName ResolvedPath = PathIdentity.IsNone() ? OutcomeTag.GetTagName() : PathIdentity;
    const TArray<FName>* PathNodes = Node->GetNextNodesForPath(ResolvedPath);
    const int32 PathCount = PathNodes ? PathNodes->Num() : 0;
    UE_LOG(LogSimpleQuestActivation, Log, TEXT("ChainToNextNodes: '%s' outcome='%s' path='%s' - %d path + %d any-outcome downstream node(s)"),
        *Node->GetContextualTag().ToString(),
        *OutcomeTag.ToString(),
        *ResolvedPath.ToString(),
        PathCount,
        Node->GetNextNodesOnAnyOutcome().Num());

    if (Node->GetContextualTag().IsValid())
    {
        SetQuestResolved(Node->GetContextualTag(), OutcomeTag, ResolvedPath, EQuestResolutionSource::Graph, OriginatingEventID);
        if (QuestSignalSubsystem)
        {
            if (FDelegateHandle* Handle = LiveStepTriggerHandles.Find(Node->GetContextualTag()))
            {
                QuestSignalSubsystem->UnsubscribeMessage(Node->GetContextualTag(), *Handle);
                LiveStepTriggerHandles.Remove(Node->GetContextualTag());
            }
        }
    }

    PublishQuestEndedEvent(Node, OutcomeTag, EQuestResolutionSource::Graph, FQuestEventPayload(), InheritedForward);

    /**
     * Thread this node's compiled ContextualTag (as FName) forward as IncomingSourceTag so any Quest destination in the next layer
     * can filter its source-qualified entries against the originator of this outcome.
     */
    const FName SourceTagName = Node->GetContextualTag().GetTagName();

    // Gather forward params from the completing step (designer-supplied via CompleteObjectiveWithOutcome)
    // and build the OriginChain extension (received chain + this step's tag) so downstream steps see the full history.
    // Seed forward parameters (activation context) from the inherited payload instead of default-constructing.
    FQuestObjectiveActivationContext ForwardPayload = InheritedForward;
    TArray<FGameplayTag> ForwardChain;
    
    // A step still overrides with its own params.
    if (const UQuestStep* CompletingStep = Cast<UQuestStep>(Node))
    {
        ForwardPayload = CompletingStep->GetCompletionForwardParams();
        ForwardChain = CompletingStep->GetReceivedActivationParams().IncomingContext.OriginChain;
    }
    if (Node->GetContextualTag().IsValid())
    {
        ForwardChain.Add(Node->GetContextualTag());
    }

    auto StampAndActivate = [this, &ForwardPayload, &ForwardChain, OutcomeTag, SourceTagName, &Node, &OriginatingEventID](const FName& DestTagName)
    {
        if (UQuestNodeBase* DestInstance = LoadedNodeInstances.FindRef(DestTagName))
        {
            DestInstance->PendingActivationContext.IncomingContext = ForwardPayload;
            DestInstance->PendingActivationContext.IncomingContext.OriginTag = Node->GetContextualTag();
            DestInstance->PendingActivationContext.IncomingContext.OriginChain = ForwardChain;
            DestInstance->PendingActivationContext.IncomingContext.OriginatingEventID = OriginatingEventID;
        }
        ActivateNodeByTag(DestTagName, EQuestActivationProvenance::ChainCascade, OutcomeTag, SourceTagName);
    };

    // Named-outcome path. PublishGraphResolutions fires unconditionally for the path's ExitedGraphTags - the
    // earlier BC-empty gate was based on the premise that the wrapper's alias-publish would cover the inner
    // asset's identity, but LinkedQuestline wrappers carry no alias to the inner asset identity in the current
    // compile model (the wrapper sits at the outer compile level with empty AssetScopedAliasPrefixes). Each
    // ChainToNextNodes layer publishes its own compile-context asset identity (Step → inner asset, wrapper →
    // outer asset); the inner-first cascade ordering invariant is preserved by the natural top-down call order.
    // Boundary completions fire next so wrapper Path facts exist before destination prereq evaluation runs;
    // direct downstream destinations activate last.
    if (const FQuestPathNodeList* PathList = Node->GetNextNodesByPath().Find(ResolvedPath))
    {
        PublishGraphResolutions(PathList->ResolvedGraphs, EQuestResolutionSource::Graph, ForwardPayload);
        for (const FQuestBoundaryCompletion& BC : PathList->BoundaryCompletions)
        {
            FireWrapperBoundaryCompletion(BC, OriginatingEventID, ForwardPayload);
        }
        for (const FName& Tag : PathList->NodeTags)
        {
            StampAndActivate(Tag);
        }
    }

    // Any-outcome path: same unconditional PublishGraphResolutions as the named-outcome branch.
    PublishGraphResolutions(Node->GetResolvedGraphsOnAnyOutcome(), EQuestResolutionSource::Graph, ForwardPayload);
    for (const FQuestBoundaryCompletion& BC : Node->GetBoundaryCompletionsOnAnyOutcome())
    {
        FireWrapperBoundaryCompletion(BC, OriginatingEventID, ForwardPayload);
    }
    for (const FName& Tag : Node->GetNextNodesOnAnyOutcome())
    {
        StampAndActivate(Tag);
    }
}

TArray<FQuestRewardPreview> UQuestManagerSubsystem::ResolveAdvertisedRewards(FGameplayTag ContentTag, FName PathIdentity, AActor* Viewer, bool bIncludeAnyOutcome) const
{
    const UQuestNodeBase* Owner = LoadedNodeInstances.FindRef(ContentTag.GetTagName());
    if (!Owner)
    {
        UE_LOG(LogSimpleQuestActivation, Verbose, TEXT("ResolveAdvertisedRewards: no loaded node for tag '%s'"), *ContentTag.ToString());
        return {};
    }

    TArray<FQuestRewardPreview> Previews = UQuestRewardNode::ResolveAdvertisedFromManifest(
        Owner->GetReachableRewardsByPath(), LoadedNodeInstances, PathIdentity, Viewer, bIncludeAnyOutcome);

    UE_LOG(LogSimpleQuestActivation, Verbose, TEXT("ResolveAdvertisedRewards: tag '%s' path '%s' (merge=%d) -> %d preview(s)"),
        *ContentTag.ToString(), *PathIdentity.ToString(), bIncludeAnyOutcome ? 1 : 0, Previews.Num());

    return Previews;
}

TMap<FGameplayTag, FQuestRewardPreviewList> UQuestManagerSubsystem::ResolveQuestlineRewards(FGameplayTag QuestlineTag, AActor* Viewer) const
{
    TMap<FGameplayTag, FQuestRewardPreviewList> Out;

    // Resolve the query tag to a reward-map identity. A caller may pass either a questline's own IDENTITY tag (standalone
    // or the harvested inner identity) or a linked placement's CONTEXTUAL tag. For the latter, the wrapper node carries
    // LinkedInnerIdentityTag - the bridge to the inner asset's identity, under which its rewards were harvested. Resolve
    // through it so a placement tag isn't a dead end to the identity-keyed reward map.
    FName IdentityName = QuestlineTag.GetTagName();
    if (const UQuestNodeBase* Node = LoadedNodeInstances.FindRef(QuestlineTag.GetTagName()))
    {
        if (Node->LinkedInnerIdentityTag.IsValid())
        {
            IdentityName = Node->LinkedInnerIdentityTag.GetTagName();
        }
    }

    const FQuestCompiledQuestlineRewards* Compiled = LiveQuestlineRewardsByIdentity.Find(IdentityName);
    if (!Compiled) return Out;

    for (const TPair<FGameplayTag, FQuestRewardSet>& Pair : Compiled->RewardsByOutcome)
    {
        FQuestRewardPreviewList List;
        for (const TObjectPtr<UQuestRewardBase>& Reward : Pair.Value.Rewards)
        {
            if (Reward) List.Previews.Append(Reward->DispatchDescribeReward(Viewer));
        }
        if (List.Previews.Num() > 0) Out.Add(Pair.Key, MoveTemp(List));
    }
    return Out;
}

TMap<FGameplayTag, FQuestRewardPreviewList> UQuestManagerSubsystem::ResolveAllAdvertisedRewardsByOutcome(FGameplayTag ContentTag, AActor* Viewer) const
{
    TMap<FGameplayTag, FQuestRewardPreviewList> Out;

    const UQuestNodeBase* Owner = LoadedNodeInstances.FindRef(ContentTag.GetTagName());
    if (!Owner) return Out;

    UGameplayTagsManager& TagManager = UGameplayTagsManager::Get();

    // One entry per STATIC-outcome path; each gets that path's rewards + the any-outcome bucket merged in (any-outcome
    // fires on every completion). Dynamic PathNames (PathIdentity not a registered tag) and NAME_None itself are skipped
    // as map keys - see the boundary-approach caveat on the header.
    for (const TPair<FName, FQuestReachableRewards>& Pair : Owner->GetReachableRewardsByPath())
    {
        if (Pair.Key.IsNone()) continue;   // the any-outcome bucket is merged INTO each outcome below, not a key itself

        const FGameplayTag OutcomeTag = TagManager.RequestGameplayTag(Pair.Key, false);
        if (!OutcomeTag.IsValid()) continue;   // dynamic PathName - no outcome tag, not BP-previewable

        // Same shared walk the point queries use: this outcome's path + the any-outcome bucket (merge=true).
        TArray<FQuestRewardPreview> Previews = UQuestRewardNode::ResolveAdvertisedFromManifest(
            Owner->GetReachableRewardsByPath(), LoadedNodeInstances, Pair.Key, Viewer, true);

        if (Previews.Num() > 0)
        {
            FQuestRewardPreviewList List;
            List.Previews = MoveTemp(Previews);
            Out.Add(OutcomeTag, MoveTemp(List));
        }
    }
    return Out;
}

void UQuestManagerSubsystem::SetQuestDeactivated(FGameplayTag QuestTag, EDeactivationSource Source, const FQuestEventPayload& Context)
{
    if (!QuestTag.IsValid() || !WorldState) return;

    // Cycle/fan-in guard. Now that deactivation passes THROUGH inactive nodes (below), the old "not active → return"
    // no longer terminates the cascade - cyclic or fan-in Deactivated→Deactivate wiring would re-forward forever.
    // Visited set is per-cascade; it clears when the root call unwinds (depth back to 0).
    if (DeactivationCascadeVisited.Contains(QuestTag))
    {
        UE_LOG(LogSimpleQuestActivation, Verbose, TEXT("SetQuestDeactivated: '%s' already in this deactivation cascade - skipping"), *QuestTag.ToString());
        return;
    }
    DeactivationCascadeVisited.Add(QuestTag);
    ++DeactivationCascadeDepth;
    ON_SCOPE_EXIT { if (--DeactivationCascadeDepth == 0) { DeactivationCascadeVisited.Reset(); } };

    // Pass-through: no Live/PendingGiver lifecycle of our own to interrupt, but still relay the teardown to
    // wired-downstream nodes. No fact write, no FQuestDeactivatedEvent, no activate-on-deactivation - an inactive
    // node forwards the cascade without claiming a transition it never made.
    if (!FQuestLifecycleQuery::HasActiveLifecycle(WorldState, QuestTag))
    {
        UE_LOG(LogSimpleQuestActivation, Verbose, TEXT("SetQuestDeactivated: '%s' not active - forwarding deactivate cascade only (pass-through)"), *QuestTag.ToString());
        CascadeDeactivation(QuestTag, Source);
        return;
    }

    // Snapshot Deactivated state before doing the cleanup work. Live/PendingGiver + Deactivated co-occurring is an
    // inconsistent state - normal flow has ActivateNodeByTag clearing Deactivated before SetQuestLive runs, so the
    // two should never be both asserted. If they are (manual fact write, save/load round-trip, race in a callback),
    // we still need to clear Live / PendingGiver - that's the actual point of deactivation. We just skip
    // re-asserting Deactivated (would bump the WorldState ref-count, leaving Deactivated pinned past a future
    // ClearBlocked / similar) and the FQuestDeactivatedEvent re-publish (subscribers already saw the original
    // transition).
    const bool bWasAlreadyDeactivated = FQuestLifecycleQuery::IsDeactivated(WorldState, QuestTag);
    if (bWasAlreadyDeactivated)
    {
        UE_LOG(LogSimpleQuestActivation, Warning,
            TEXT("SetQuestDeactivated: '%s' had Live or PendingGiver AND Deactivated asserted simultaneously - inconsistent state; clearing active facts, skipping Deactivated fact-write + event re-publish"),
            *QuestTag.ToString());
    }
    
    RecentGiverActors.Remove(QuestTag);
    
	const FName TagName = QuestTag.GetTagName();

	UE_LOG(LogSimpleQuestActivation, Log, TEXT("SetQuestDeactivated: '%s' source=%s"),
		*QuestTag.ToString(),
		Source == EDeactivationSource::External ? TEXT("External") : TEXT("Internal"));

	// Look up node instance early. Needed for DeactivateInternal and context assembly.
	UQuestNodeBase* Node = nullptr;
	if (TObjectPtr<UQuestNodeBase>* NodePtr = LoadedNodeInstances.Find(TagName))
	{
		Node = *NodePtr;
	}

    // PendingGiver cleanup. Don't mutate RegisteredGiverQuestTags - it's the structural "this quest has a giver"
    // set, sticky across the session. After deactivation, the next activation re-engages the giver gate
    // normally because the tag is still in the set.
    if (FQuestLifecycleQuery::IsPendingGiver(WorldState, QuestTag))
    {
        ClearQuestPendingGiver(QuestTag);
    }

    // Enablement watch cleanup: Defensive against entry persisting after deactivation.
    ClearEnablementWatch(QuestTag);

    // Active node cleanup: Use Node instead of redundant lookup
    if (FQuestLifecycleQuery::IsLive(WorldState, QuestTag))
    {
        if (Node)
        {
            Node->DeactivateInternal(QuestTag);
        }

        // Only Steps clear the Live fact directly. Containers' Live is derived from inner Step state; the cascading
        // deactivation routed through NextNodesToDeactivateOnDeactivation will eventually deactivate inner Steps, and
        // each Step's SetQuestDeactivated walks its ancestors to re-derive container Live. Skipping the direct removal
        // here preserves the invariant that container Live always reflects inner Step state, even briefly between this
        // method and the inner-Step cascade.
        if (!Node || !Node->IsContainerNode())
        {
            RemoveStateFactAcrossPerspectives(QuestTag, EQuestStateLeaf::Live);
        }

        if (FDelegateHandle* Handle = LiveStepTriggerHandles.Find(QuestTag))
        {
            if (QuestSignalSubsystem) QuestSignalSubsystem->UnsubscribeMessage(QuestTag, *Handle);
            LiveStepTriggerHandles.Remove(QuestTag);
        }

        if (TMap<FGameplayTag, FPrereqLeafSubscription::FPrereqLeafHandles>* Handles = DeferredCompletionPrereqHandles.Find(QuestTag))
        {
            FPrereqLeafSubscription::UnsubscribeAll(QuestSignalSubsystem, *Handles);
            DeferredCompletionPrereqHandles.Remove(QuestTag);
        }
        DeferredCompletions.Remove(QuestTag);

        // Ancestor walk for Steps. After the Step's Live fact has been cleared, each ancestor container
        // re-derives its Live state. Containers skip - they don't own a Live fact directly to walk away from.
        if (Node && Node->IsStepNode())
        {
            DeriveAllAncestorContainersForStep(Cast<UQuestStep>(Node));
        }
    }

    // Skip the Deactivated fact write + event publish if Deactivated was already asserted (the ref-count bump
    // would prevent a future ClearBlocked from fully clearing Deactivated; the event re-publish would deliver a
    // duplicate signal). Live / PendingGiver cleanup above ran unconditionally - that's the actual point of this
    // function.
    if (!bWasAlreadyDeactivated)
    {
        AddStateFactAcrossPerspectives(QuestTag, EQuestStateLeaf::Deactivated);

        // Publish with context. Caller-supplied Context (from BP request) overlays onto the framework-assembled
        // context when a Node is loaded - Instigator / CustomData / lineage from the request layer in; NodeInfo
        // stays from assembly. No-Node fallback path uses the request Context directly (nothing else to merge).
        if (QuestSignalSubsystem)
        {
            if (Node)
            {
                FQuestEventPayload EffectiveContext = OverlayCallerContext(AssembleEventContext(Node, FQuestObjectiveTriggerContext()), Context);
                FQuestDeactivatedEvent Event(QuestTag, Source, EffectiveContext);
                FQuestPublish::OnAllNodeTags(QuestSignalSubsystem, Node, Event);

                // Trigger-side wrap on interruption. Steps only - containers don't host trigger components. FinalContext
                // empty because deactivation has no specific fire driving it; OutcomeTag invalid for the same reason.
                if (Node->IsStepNode())
                {
                    FQuestPublish::OnAllNodeTags(QuestSignalSubsystem, Node, FQuestTriggerDeactivatedEvent(
                        QuestTag, EQuestTriggerEndReason::Interrupted, FGameplayTag(), FQuestObjectiveTriggerContext()));
                }
            }
            else
            {
                // Fallback - no instance loaded under this tag. Single publish preserves observability.
                QuestSignalSubsystem->PublishMessage(QuestTag, FQuestDeactivatedEvent(QuestTag, Source, Context));
            }
        }
    }

    // Relay the teardown downstream after our own deactivation. Same call the pass-through branch makes, so active
    // and inactive nodes forward identically - moved off the bus event so the no-publish path still cascades.
    CascadeDeactivation(QuestTag, Source);
}

void UQuestManagerSubsystem::CascadeDeactivation(FGameplayTag QuestTag, EDeactivationSource Source)
{
    UQuestNodeBase* Node = LoadedNodeInstances.FindRef(QuestTag.GetTagName());
    if (!Node) return;

    // Each compile-time FName is in the source node's compile-context perspective; ResolveToCanonicalTag converts to
    // the perspective IsDeactivated lookups use. Recurses into SetQuestDeactivated, whose visited guard breaks cycles.
    for (const FName& Tag : Node->GetNextNodesToDeactivateOnDeactivation())
    {
        const FGameplayTag TargetTag = UGameplayTagsManager::Get().RequestGameplayTag(Tag, false);
        const FGameplayTag CanonicalTarget = ResolveToCanonicalTag(TargetTag);
        if (CanonicalTarget.IsValid()) SetQuestDeactivated(CanonicalTarget, Source);
    }
}

void UQuestManagerSubsystem::HandleNodeDeactivatedEvent(FGameplayTag Channel, const FQuestDeactivatedEvent& Event)
{
    const FName TagName = Channel.GetTagName();
    TObjectPtr<UQuestNodeBase>* NodePtr = LoadedNodeInstances.Find(TagName);
    if (!NodePtr) return;

    UQuestNodeBase* Node = *NodePtr;

    UE_LOG(LogSimpleQuestActivation, Log, TEXT("HandleNodeDeactivatedEvent: '%s' - activating %d, cascading deactivation to %d"),
        *Channel.ToString(),
        Node->GetNextNodesOnDeactivation().Num(),
        Node->GetNextNodesToDeactivateOnDeactivation().Num());
    
    // Activate-on-deactivation only (Deactivated → Activate). This stays event-driven and therefore transition-only:
    // the FQuestDeactivatedEvent that lands here is published solely for nodes that actually deactivated, so a
    // pass-through node never fires a spurious handoff-activation. The Deactivated → Deactivate cascade moved to
    // SetQuestDeactivated (CascadeDeactivation) so it forwards on the pass-through path too.
    for (const FName& Tag : Node->GetNextNodesOnDeactivation())
    {
        ActivateNodeByTag(Tag, EQuestActivationProvenance::DeactivationCascade);
    }
}

void UQuestManagerSubsystem::PublishQuestEndedEvent(const UQuestNodeBase* Node, FGameplayTag OutcomeTag, EQuestResolutionSource Source, const FQuestEventPayload& ExternalContext, const FQuestObjectiveActivationContext& CompleterContext) const
{
    if (!QuestSignalSubsystem || !Node->GetContextualTag().IsValid()) return;

    FQuestObjectiveTriggerContext CompletionCtx;
    if (const UQuestStep* Step = Cast<UQuestStep>(Node))
    {
        CompletionCtx = Step->GetCompletionContext();
    }
    else
    {
        // Container end: no step-side completion context. Copy the attribution the ended event reads, from the completer
        // threaded across the boundary. TriggeredActor has no single-actor analog for a container, so it stays empty.
        CompletionCtx.Instigator = CompleterContext.Instigator;
        CompletionCtx.CustomData = CompleterContext.CustomData;
        CompletionCtx.CustomTag  = CompleterContext.CustomTag;
    }

    FQuestEventPayload Context = OverlayCallerContext(AssembleEventContext(Node, CompletionCtx), ExternalContext);
    FQuestPublish::OnAllNodeTags(QuestSignalSubsystem, Node, FQuestEndedEvent(Node->GetContextualTag(), OutcomeTag, Source, Context));

    // Trigger-side completion: per-fire Response(Completed) + per-lifecycle Deactivated(Completed). Steps only - containers
    // don't host trigger components. CompletionCtx echoes the fire context the objective drove completion with so trigger
    // subscribers' own-fire filter (TriggeredActor == GetOwner()) resolves on the round trip.
    if (Node->IsStepNode())
    {
        FQuestPublish::OnAllNodeTags(QuestSignalSubsystem, Node, FQuestTriggerResponseEvent(Node->GetContextualTag(), EQuestTriggerResolution::Completed, OutcomeTag, FGameplayTag(), CompletionCtx));
        FQuestPublish::OnAllNodeTags(QuestSignalSubsystem, Node, FQuestTriggerDeactivatedEvent(Node->GetContextualTag(), EQuestTriggerEndReason::Completed, OutcomeTag, CompletionCtx));
    }
}

void UQuestManagerSubsystem::HandleGiveQuestEvent(FGameplayTag Channel, const FQuestGivenEvent& Event)
{
    const FGameplayTag QuestTag = Event.GetQuestTag();
    if (!QuestTag.IsValid()) return;

    // Blocker gate - quest-level identity check, evaluated against the input tag (the standalone-perspective form
    // the giver actor authored against). State subsystem owns blocker computation; we read its result. Refused
    // gives don't disrupt the quest's PendingGiver state.
    UQuestStateSubsystem* StateSubsystem = GetGameInstance() ? GetGameInstance()->GetSubsystem<UQuestStateSubsystem>() : nullptr;
    TArray<FQuestActivationBlocker> Blockers;
    if (StateSubsystem)
    {
        Blockers = StateSubsystem->QueryQuestActivationBlockers(QuestTag);
    }
    if (!Blockers.IsEmpty())
    {
        FQuestPublish::OnAllTagsForRequest(QuestSignalSubsystem, QuestTag, LoadedNodeInstances, FQuestGiveBlockedEvent(QuestTag, Blockers, Event.Params.Instigator.Get()));

        // Build the debug warning message, showing each blocker in a bulleted list, with sub-bullets for specific unmet prereq leaves
        FString Message = FString::Printf(TEXT("HandleGiveQuestEvent: '%s' refused - %d blocker%s:"),
            *QuestTag.ToString(),
            Blockers.Num(),
            Blockers.Num() == 1 ? TEXT("") : TEXT("s"));

        // Bullet per blocker
        const UEnum* BlockerEnum = StaticEnum<EQuestActivationBlocker>();
        for (const FQuestActivationBlocker& Blocker : Blockers)
        {
            const FString ReasonName = BlockerEnum
                ? BlockerEnum->GetNameStringByValue(static_cast<int64>(Blocker.Reason)) // drop the scope identifier with GetNameStringByValue
                : FString::FromInt(static_cast<int32>(Blocker.Reason));                 // fallback to the numeric index if lookup fails
            Message += FString::Printf(TEXT("\n  • %s"), *ReasonName);

            // Build sub-bullets for unmet prereqs
            if (Blocker.Reason == EQuestActivationBlocker::PrereqUnmet && !Blocker.UnsatisfiedLeafTags.IsEmpty())
            {
                for (const FGameplayTag& LeafTag : Blocker.UnsatisfiedLeafTags)
                {
                    Message += FString::Printf(TEXT("\n      - %s"), *LeafTag.ToString());
                }
            }
        }
        // Log it.
        UE_LOG(LogSimpleQuestActivation, Warning, TEXT("%s"), *Message);
        return;
    }

    // Resolve canonical tags so each placement (standalone + every aliased contextual) gets the give-completion
    // side effects independently. Single-instance case (no LinkedQuestline placements) collapses to one iteration.
    // Without this, a giver firing for a quest reached via a LinkedQuestline would only target the standalone
    // placement and leave the inlined contextual stuck in PendingGiver. RegisteredGiverQuestTags is the structural
    // "this quest has a giver" set, populated at startup and never mutated at runtime; bBypassGiverGate=true on the
    // ActivateNodeByTag below routes past the gate without re-entering PendingGiver while leaving the set intact
    // for the next loop iteration / external re-activation.
    TArray<FGameplayTag> CanonicalTags = StateSubsystem ? StateSubsystem->ResolveCanonicalTags(QuestTag) : TArray<FGameplayTag>{ QuestTag };

    UE_LOG(LogSimpleQuestActivation, Log, TEXT("HandleGiveQuestEvent: '%s' - clearing PendingGiver, activating %d placement(s) (CustomData %s, Instigator %s)"),
        *QuestTag.ToString(),
        CanonicalTags.Num(),
        Event.Params.CustomData.IsValid() ? TEXT("populated") : TEXT("empty"),
        Event.Params.Instigator.IsValid() ? *Event.Params.Instigator->GetName() : TEXT("null"));

    // Deduplication by Instance pointer - same rationale as HandleActivationRequest's loop. ResolveCanonicalTags returns
    // multiple entries when input is an alias; for multi-alias-of-single-Instance scenarios all entries resolve
    // to the same pointer and the per-canonical work would be redundantly applied to the same Instance. State
    // writes (ClearQuestPendingGiver) already fan out across perspectives via RemoveStateFactAcrossPerspectives;
    // the attribution block already iterates Instance.GetAssetScopedAliasTags so all perspectives are covered in
    // one iteration. Deduplication keeps multi-PLACEMENT iterations distinct since their pointers genuinely differ.
    TSet<UQuestNodeBase*> SeenInstances;
    for (const FGameplayTag& CanonicalTag : CanonicalTags)
    {
        if (!CanonicalTag.IsValid()) continue;

        UQuestNodeBase* Instance = LoadedNodeInstances.FindRef(CanonicalTag.GetTagName());
        if (!Instance) continue;  // Skip canonicals with no loaded instance - avoids ActivateNodeByTag's noisy warning.
        if (SeenInstances.Contains(Instance)) continue;
        SeenInstances.Add(Instance);

        if (Event.Params.Instigator.IsValid())
        {
            // Multi-perspective attribution: a give issued against one perspective tag must attribute the
            // GiverActor regardless of which perspective HandleOnNodeStarted later sees as Node->GetContextual-
            // Tag(). The same logical Step can have a ContextualTag from one graph perspective and aliases from
            // others; ResolveCanonicalTags returns the perspective the give was authored against, which isn't
            // necessarily the Instance's own ContextualTag. Write the giver to every tag the Instance is known
            // by so the lookup hits regardless of which perspective surfaces at the FQuestStartedEvent publish.
            RecentGiverActors.Add(CanonicalTag, Event.Params.Instigator);

            const FGameplayTag InstanceContextualTag = Instance->GetContextualTag();
            if (InstanceContextualTag.IsValid() && InstanceContextualTag != CanonicalTag)
            {
                RecentGiverActors.Add(InstanceContextualTag, Event.Params.Instigator);
            }
            for (const FGameplayTag& AliasTag : Instance->GetAssetScopedAliasTags())
            {
                if (AliasTag.IsValid() && AliasTag != CanonicalTag && AliasTag != InstanceContextualTag)
                {
                    RecentGiverActors.Add(AliasTag, Event.Params.Instigator);
                }
            }
        }

        ClearQuestPendingGiver(CanonicalTag);
        ClearEnablementWatch(CanonicalTag);

        // Mirror of HandleActivationRequest: stash the giver-authored params on the target step so ActivateInternal
        // merges them with the step's authored defaults. Empty Params stamps cleanly. Additive merge preserves the
        // step's defaults in that case.
        if (UQuestStep* Step = Cast<UQuestStep>(Instance))
        {
            Step->PendingActivationContext.IncomingContext = Event.Params;
        }

        ActivateNodeByTag(CanonicalTag.GetTagName(), EQuestActivationProvenance::GiverGate, FGameplayTag(), NAME_None, true);
    }
}

void UQuestManagerSubsystem::HandleActivationRequest(FGameplayTag Channel, const FQuestActivationRequestEvent& Event)
{
    const FGameplayTag QuestTag = Event.GetQuestTag();
    if (!QuestTag.IsValid()) return;

    // Resolve canonical tags so a request authored against the standalone-perspective form (the natural BP-side
    // authoring tag) reaches every active placement - standalone + every aliased contextual. Without this, an
    // external RequestActivation against an alias-form tag only targets the standalone placement and leaves
    // inlined contextual placements stranded. Single-instance / non-aliased case collapses to one iteration.
    UQuestStateSubsystem* StateSubsystem = GetGameInstance() ? GetGameInstance()->GetSubsystem<UQuestStateSubsystem>() : nullptr;
    TArray<FGameplayTag> CanonicalTags = StateSubsystem ? StateSubsystem->ResolveCanonicalTags(QuestTag) : TArray<FGameplayTag>{ QuestTag };

    UE_LOG(LogSimpleQuestActivation, Log, TEXT("HandleActivationRequest: '%s' - resolved to %d canonical(s) to try (CustomData %s)"),
        *QuestTag.ToString(),
        CanonicalTags.Num(),
        Event.Params.CustomData.IsValid() ? TEXT("populated") : TEXT("empty"));

    // Deduplication by Instance pointer. ResolveCanonicalTags returns multiple entries when the input tag is registered
    // as an alias (input + every canonical the alias represents). For multi-PLACEMENT scenarios (standalone +
    // LinkedQuestline inlined contextual placements) these resolve to distinct Instances and each iteration
    // dispatches a real, distinct activation. For multi-alias-of-single-Instance scenarios the entries all
    // resolve to the same pointer; without deduplication, ActivateNodeByTag is called once per redundant alias, hitting
    // the diamond / Block guards on subsequent iterations and producing duplicate FQuestActivationFailedEvent
    // publishes - one per over-iteration. Deduplication by pointer collapses those into a single dispatch.
    TSet<UQuestNodeBase*> SeenInstances;
    int32 SuccessfulDispatches = 0;
    for (const FGameplayTag& CanonicalTag : CanonicalTags)
    {
        if (!CanonicalTag.IsValid()) continue;

        UQuestNodeBase* Instance = LoadedNodeInstances.FindRef(CanonicalTag.GetTagName());
        if (!Instance) continue;
        if (SeenInstances.Contains(Instance)) continue;
        SeenInstances.Add(Instance);

        if (UQuestStep* Step = Cast<UQuestStep>(Instance))
        {
            Step->PendingActivationContext.IncomingContext = Event.Params;
        }

        ActivateNodeByTag(CanonicalTag.GetTagName(), EQuestActivationProvenance::ExternalAPI, FGameplayTag(), NAME_None, false, Event.bBypassPrerequisites);
        ++SuccessfulDispatches;
    }
    
    if (SuccessfulDispatches == 0 && QuestSignalSubsystem)
    {
        UE_LOG(LogSimpleQuestActivation, Warning, TEXT("HandleActivationRequest: '%s' - no placements reached an activation (zero loaded instances among %d canonical(s))"),
            *QuestTag.ToString(), CanonicalTags.Num());

        FQuestEventPayload Payload;
        Payload.Instigator = Event.Params.Instigator;
        Payload.CustomData = Event.Params.CustomData;
        Payload.OriginTag = Event.Params.OriginTag;
        Payload.OriginChain = Event.Params.OriginChain;
        Payload.OriginatingEventID = Event.Params.OriginatingEventID;
        // Payload.NodeInfo stays default (no node identity to forward for UnknownQuest).

        PublishUnknownQuestFailure(QuestSignalSubsystem, LoadedNodeInstances, QuestTag.GetTagName(), Payload);
    }
}

void UQuestManagerSubsystem::HandleBlockRequest(FGameplayTag Channel, const FQuestBlockRequestEvent& Event)
{
    // Resolve input to canonical so the Blocked fact is written at the perspective queries alias-walk to find.
    // Without this, an alias-form input writes State.<alias>.Blocked which no query touches, and the quest
    // appears unblocked everywhere despite the request succeeding.
    const FGameplayTag QuestTag = ResolveSingleCanonicalForMutation(Event.GetQuestTag());
    if (!QuestTag.IsValid() || !WorldState) return;

    // Block-side: idempotency guard at canonical perspective. Symmetric with the already-deactivated guard in
    // SetQuestDeactivated. Spamming a block request on an already-blocked quest would otherwise bump the
    // WorldState ref-count without reflecting a genuine state transition. Also gates FQuestBlockedEvent
    // broadcast so already-blocked re-applications stay silent at the event layer.
    if (FQuestLifecycleQuery::IsBlocked(WorldState, QuestTag))
    {
        UE_LOG(LogSimpleQuestActivation, Verbose, TEXT("HandleBlockRequest: '%s' skipped - already blocked"), *QuestTag.ToString());
    }
    else
    {
        AddStateFactAcrossPerspectives(QuestTag, EQuestStateLeaf::Blocked);
        FQuestPublish::OnAllTagsForRequest(QuestSignalSubsystem, QuestTag, LoadedNodeInstances, FQuestBlockedEvent(QuestTag, Event.Source, Event.Context));

        UE_LOG(LogSimpleQuestActivation, Log, TEXT("HandleBlockRequest: '%s' - Blocked fact added, FQuestBlockedEvent published (source=%s)"),
            *QuestTag.ToString(),
            Event.Source == EDeactivationSource::External ? TEXT("External") : TEXT("Internal"));
    }

    // Deactivate-side: independent of block-idempotency outcome. Matches USetBlockedNode's "publish both events;
    // each handler's own idempotency decides" pattern, giving graph-driven and event-driven paths parity.
    // HandleDeactivateRequest applies its own already-deactivated guard, so a no-op block + no-op deactivate
    // combination stays silent at both event layers.
    if (Event.bAlsoDeactivate && QuestSignalSubsystem)
    {
        QuestSignalSubsystem->PublishMessage(Tag_Channel_QuestDeactivateRequest,
            FQuestDeactivateRequestEvent(QuestTag, Event.Source, Event.Context));
        UE_LOG(LogSimpleQuestActivation, Verbose, TEXT("HandleBlockRequest: '%s' - bAlsoDeactivate=true → published DeactivateRequest"),
            *QuestTag.ToString());
    }
}

void UQuestManagerSubsystem::HandleClearBlockRequest(FGameplayTag Channel, const FQuestClearBlockRequestEvent& Event)
{
    // Resolve input to canonical so the Blocked-fact clear lands at the perspective the write went to.
    const FGameplayTag QuestTag = ResolveSingleCanonicalForMutation(Event.GetQuestTag());
    if (!QuestTag.IsValid() || !WorldState) return;

    // Symmetric with the already-blocked guard in HandleBlockRequest. Also gates FQuestUnblockedEvent broadcast
    // so clear-on-already-unblocked stays silent at the event layer.
    if (!FQuestLifecycleQuery::IsBlocked(WorldState, QuestTag))
    {
        UE_LOG(LogSimpleQuestActivation, Verbose, TEXT("HandleClearBlockRequest: '%s' skipped - not currently blocked"), *QuestTag.ToString());
        return;
    }

    RemoveStateFactAcrossPerspectives(QuestTag, EQuestStateLeaf::Blocked);
    // Deactivated intentionally not cleared: the target's Activate input clears it on re-entry.

    // Multi-channel publish - mirrors HandleBlockRequest's OnAllTagsForRequest path so subscribers on any
    // perspective of this Step (ContextualTag or any AssetScopedAliasTag) receive one callback via the
    // bus's per-subscriber dedup. Without this, an observer subscribed via one perspective misses unblock
    // events published on a sibling perspective's canonical.
    FQuestPublish::OnAllTagsForRequest(QuestSignalSubsystem, QuestTag, LoadedNodeInstances, FQuestUnblockedEvent(QuestTag, Event.Source, Event.Context));

    UE_LOG(LogSimpleQuestActivation, Log, TEXT("HandleClearBlockRequest: '%s' - Blocked fact cleared, FQuestUnblockedEvent published (source=%s)"),
        *QuestTag.ToString(),
        Event.Source == EDeactivationSource::External ? TEXT("External") : TEXT("Internal"));
}

void UQuestManagerSubsystem::HandleResolveRequest(FGameplayTag Channel, const FQuestResolveRequestEvent& Event)
{
    // Resolve input to canonical so IsTerminal and SetQuestResolved target the perspective state facts live at.
    const FGameplayTag QuestTag = ResolveSingleCanonicalForMutation(Event.GetQuestTag());
    if (!QuestTag.IsValid() || !WorldState) return;

    // Override guard - skip if already in a terminal state unless designer explicitly opts in. Default-false
    // protects against accidental double-broadcast; opt-in true appends additively (never removes prior facts).
    if (!Event.bOverrideExisting && FQuestLifecycleQuery::IsTerminal(WorldState, QuestTag))
    {
        UE_LOG(LogSimpleQuestActivation, Warning,
            TEXT("HandleResolveRequest: '%s' skipped - already in terminal state. Pass bOverrideExisting=true to append a new resolution entry additively."),
            *QuestTag.ToString());
        return;
    }
    
    SetQuestResolved(QuestTag, Event.OutcomeTag, NAME_None, EQuestResolutionSource::External);

    // Live-step bookkeeping cleanup mirroring ChainToNextNodes - defensive against the non-Live cases (Find returns null).
    if (QuestSignalSubsystem)
    {
        if (FDelegateHandle* Handle = LiveStepTriggerHandles.Find(QuestTag))
        {
            QuestSignalSubsystem->UnsubscribeMessage(QuestTag, *Handle);
            LiveStepTriggerHandles.Remove(QuestTag);
        }
        if (TMap<FGameplayTag, FPrereqLeafSubscription::FPrereqLeafHandles>* Handles = DeferredCompletionPrereqHandles.Find(QuestTag))
        {
            FPrereqLeafSubscription::UnsubscribeAll(QuestSignalSubsystem, *Handles);
            DeferredCompletionPrereqHandles.Remove(QuestTag);
        }
        DeferredCompletions.Remove(QuestTag);
    }
    ClearEnablementWatch(QuestTag);

    // Publish FQuestEndedEvent - branch on whether a node instance is loaded for context assembly. Either
    // path threads the request's Context into the published event: Node-loaded overlays caller's attribution
    // onto the assembled context; no-Node uses the caller's Context directly.
    if (QuestSignalSubsystem)
    {
        if (UQuestNodeBase* Node = LoadedNodeInstances.FindRef(QuestTag.GetTagName()))
        {
            PublishQuestEndedEvent(Node, Event.OutcomeTag, EQuestResolutionSource::External, Event.Context);
        }
        else
        {
            // Fully-dynamic flow - no node instance. Publish a minimal event without assembled Context.
            QuestSignalSubsystem->PublishMessage(QuestTag, FQuestEndedEvent(QuestTag, Event.OutcomeTag, EQuestResolutionSource::External, Event.Context));
        }
    }

    UE_LOG(LogSimpleQuestActivation, Log, TEXT("HandleResolveRequest: '%s' resolved with outcome='%s' (override=%d)"),
        *QuestTag.ToString(), *Event.OutcomeTag.ToString(), Event.bOverrideExisting ? 1 : 0);
}

void UQuestManagerSubsystem::HandleQuestlineStartRequest(FGameplayTag Channel, const FQuestlineStartRequestEvent& Event)
{
    if (Event.Graph.IsNull())
    {
        UE_LOG(LogSimpleQuestActivation, Warning, TEXT("HandleQuestlineStartRequest: null graph reference, skipping"));
        return;
    }

    UE_LOG(LogSimpleQuestActivation, Log, TEXT("HandleQuestlineStartRequest: '%s' - load and %s"),
        *Event.Graph.ToString(), Event.bRestoreFromSave ? TEXT("restore") : TEXT("activate"));

    AsyncLoadAndActivate<UQuestlineGraph>(this, Event.Graph,
        [this, Params = Event.Params, bRestore = Event.bRestoreFromSave](UQuestlineGraph* Graph)
        {
            if (!Graph)
            {
                UE_LOG(LogSimpleQuestActivation, Warning, TEXT("HandleQuestlineStartRequest: load completed but graph still null"));
                return;
            }
            if (bRestore)
            {
                UE_LOG(LogSimpleQuestActivation, Log, TEXT("HandleQuestlineStartRequest: restoring '%s'"), *Graph->GetName());
                RestoreQuestlineGraph(Graph);
            }
            else
            {
                UE_LOG(LogSimpleQuestActivation, Log, TEXT("HandleQuestlineStartRequest: activating '%s'"), *Graph->GetName());
                ActivateQuestlineGraph(Graph, Params);
            }
        });
}

void UQuestManagerSubsystem::RegisterGiversFromAssetRegistry()
{
#if WITH_EDITOR
    for (TObjectIterator<UBlueprint> It; It; ++It)
    {
        const UBlueprint* Blueprint = *It;
        if (!Blueprint->GeneratedClass) continue;

        const AActor* CDO = Cast<AActor>(Blueprint->GeneratedClass->GetDefaultObject());
        if (!CDO) continue;

        const UQuestGiverComponent* GiverComp = CDO->FindComponentByClass<UQuestGiverComponent>();
        if (!GiverComp) continue;

        for (const FGameplayTag& Tag : GiverComp->GetQuestTagsToGive())
        {
            if (!Tag.IsValid()) continue;
            RegisteredGiverQuestTags.Add(Tag);
            UE_LOG(LogSimpleQuestActivation, Verbose,
                TEXT("UQuestManagerSubsystem::RegisterGiversFromAssetRegistry : registered giver for '%s' from '%s' (in-memory)"),
                *Tag.ToString(), *Blueprint->GetName());
        }
    }
#else
    IAssetRegistry& AR = FAssetRegistryModule::GetRegistry();

    FARFilter Filter;
    Filter.ClassPaths.Add(FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("Blueprint")));
    Filter.bRecursiveClasses = true;

    TArray<FAssetData> Blueprints;
    AR.GetAssets(Filter, Blueprints);

    for (const FAssetData& Asset : Blueprints)
    {
        FString TagValue;
        if (!Asset.GetTagValue(TEXT("QuestTagsToGive"), TagValue) || TagValue.IsEmpty())
            continue;

        TArray<FString> TagStrings;
        TagValue.ParseIntoArray(TagStrings, TEXT(","));

        for (const FString& TagStr : TagStrings)
        {
            const FGameplayTag ContextualTag = UGameplayTagsManager::Get().RequestGameplayTag(FName(*TagStr), false);
            if (!ContextualTag.IsValid())
            {
                UE_LOG(LogSimpleQuestActivation, Warning,
                    TEXT("UQuestManagerSubsystem::RegisterGiversFromAssetRegistry : tag '%s' is not registered - has the questline been compiled?"),
                    *TagStr);
                continue;
            }
            RegisteredGiverQuestTags.Add(ContextualTag);
            UE_LOG(LogSimpleQuestActivation, Verbose,
                TEXT("UQuestManagerSubsystem::RegisterGiversFromAssetRegistry : registered giver for '%s' from '%s' (asset registry)"),
                *ContextualTag.ToString(), *Asset.AssetName.ToString());
        }
    }
#endif
}

void UQuestManagerSubsystem::BuildListenerGroupIndex()
{
    IAssetRegistry& AR = FAssetRegistryModule::GetRegistry();

    FARFilter Filter;
    Filter.ClassPaths.Add(UQuestlineGraph::StaticClass()->GetClassPathName());
    Filter.bRecursiveClasses = true;

    TArray<FAssetData> Assets;
    AR.GetAssets(Filter, Assets);

    int32 IndexedAssetCount = 0;
    int32 IndexedTagCount = 0;
    for (const FAssetData& Asset : Assets)
    {
        FString TagValue;
        if (!Asset.GetTagValue(TEXT("ListenerGroupTags"), TagValue) || TagValue.IsEmpty()) continue;

        const FSoftObjectPath AssetPath(Asset.GetSoftObjectPath());

        TArray<FString> TagStrings;
        TagValue.ParseIntoArray(TagStrings, TEXT("|"), true);

        for (const FString& TagStr : TagStrings)
        {
            const FGameplayTag GroupTag = UGameplayTagsManager::Get().RequestGameplayTag(FName(*TagStr), false);
            if (!GroupTag.IsValid()) continue;

            GraphsByListenerGroupTag.FindOrAdd(GroupTag).AddUnique(AssetPath);
            ++IndexedTagCount;
        }
        ++IndexedAssetCount;
    }

    UE_LOG(LogSimpleQuestActivation, Log,
        TEXT("BuildListenerGroupIndex: scanned %d UQuestlineGraph asset(s); indexed %d listener-bearing graph(s) under %d (GroupTag, graph) entries"),
        Assets.Num(),
        IndexedAssetCount,
        IndexedTagCount);
}

void UQuestManagerSubsystem::WarmReachableGraphs(UQuestlineGraph* Graph)
{
    if (!Graph) return;

    // Mark this graph as loaded BEFORE walking - guards against the (rare) case where a listener graph's own setters
    // loop back to this graph, which would otherwise re-trigger an async-load for an already-resident asset.
    KnownLoadedGraphPaths.Add(FSoftObjectPath(Graph));

    for (const FGameplayTag& SetterTag : Graph->OutwardSetterGroupTags)
    {
        const TArray<FSoftObjectPath>* ListenerGraphs = GraphsByListenerGroupTag.Find(SetterTag);
        if (!ListenerGraphs) continue;

        for (const FSoftObjectPath& ListenerPath : *ListenerGraphs)
        {
            if (KnownLoadedGraphPaths.Contains(ListenerPath)) continue;

            // Pre-mark to prevent a parallel fan-in (multiple WarmReachableGraphs invocations during the same cascade
            // tick triggering N async-loads for the same target). The actual load + RegisterQuestlineGraph happens in
            // the completion callback; subsequent WarmReachableGraphs walks see the path already in the set and skip.
            KnownLoadedGraphPaths.Add(ListenerPath);

            UE_LOG(LogSimpleQuestActivation, Log,
                TEXT("WarmReachableGraphs: '%s' setter on '%s' targets listener graph '%s' - async-loading"),
                *Graph->GetName(),
                *SetterTag.ToString(),
                *ListenerPath.ToString());

            const TSoftObjectPtr<UQuestlineGraph> SoftListener(ListenerPath);
            AsyncLoadAndActivate<UQuestlineGraph>(this, SoftListener,
                [this, ListenerPath](UQuestlineGraph* ListenerGraph)
                {
                    if (ListenerGraph)
                    {
                        UE_LOG(LogSimpleQuestActivation, Log,
                            TEXT("WarmReachableGraphs: load complete for '%s' - registering"),
                            *ListenerGraph->GetName());
                        RegisterQuestlineGraph(ListenerGraph);  // Recursively cascades via its own WarmReachableGraphs.
                    }
                    else
                    {
                        UE_LOG(LogSimpleQuestActivation, Warning,
                            TEXT("WarmReachableGraphs: load returned null for '%s'"),
                            *ListenerPath.ToString());
                    }
                });
        }
    }
}

void UQuestManagerSubsystem::CheckClassObjectives(FGameplayTag Channel, const FInstancedStruct& RawEvent)
{
    const FQuestTriggerFiredEvent* Event = RawEvent.GetPtr<FQuestTriggerFiredEvent>();
    if (!Event || !Event->TriggeredActor || !QuestSignalSubsystem) return;

    for (const auto& Pair : ClassFilteredSteps)
    {
        if (Event->TriggeredActor->IsA(Pair.Value))
        {
            UE_LOG(LogSimpleQuestActivation, Verbose, TEXT("CheckClassObjectives: actor '%s' (%s) matches class filter - forwarding to step '%s'"),
                *Event->TriggeredActor->GetName(),
                *Pair.Value->GetName(),
                *Pair.Key.ToString());

            // Re-publish on the step's channel, preserving the full derived struct
            QuestSignalSubsystem->PublishRawMessage(Pair.Key, RawEvent);
        }
    }
}

void UQuestManagerSubsystem::DeferChainToNextNodes(UQuestStep* Step, FGameplayTag OutcomeTag, FName PathIdentity)
{
    const FGameplayTag StepTag = Step->GetContextualTag();
    DeferredCompletions.Add(StepTag, FQuestDeferredCompletion{ OutcomeTag, PathIdentity });

    TMap<FGameplayTag, FPrereqLeafSubscription::FPrereqLeafHandles>& Handles = DeferredCompletionPrereqHandles.FindOrAdd(StepTag);
    FPrereqLeafSubscription::SubscribeLeavesForReevaluation(
        Step->PrerequisiteExpression,
        this,
        &UQuestManagerSubsystem::OnDeferredCompletionPrereqAdded,
        &UQuestManagerSubsystem::OnDeferredCompletionPrereqResolutionRecorded,
        &UQuestManagerSubsystem::OnDeferredCompletionPrereqEntryRecorded,
        Handles);

    UE_LOG(LogSimpleQuestActivation, Log, TEXT("DeferChainToNextNodes: '%s' outcome='%s' path='%s' - subscribed to %d prereq channel(s)"),
        *StepTag.ToString(),
        *OutcomeTag.ToString(),
        *PathIdentity.ToString(),
        Handles.Num());
}

void UQuestManagerSubsystem::OnDeferredCompletionPrereqAdded(FGameplayTag Channel, const FWorldStateFactAddedEvent& Event)
{
    TryFireAllDeferredCompletions();
}

void UQuestManagerSubsystem::OnDeferredCompletionPrereqResolutionRecorded(FGameplayTag Channel, const FQuestResolutionRecordedEvent& Event)
{
    TryFireAllDeferredCompletions();
}

void UQuestManagerSubsystem::OnDeferredCompletionPrereqEntryRecorded(FGameplayTag Channel, const FQuestEntryRecordedEvent& Event)
{
    TryFireAllDeferredCompletions();
}

void UQuestManagerSubsystem::TryFireAllDeferredCompletions()
{
    // Try every deferred step: the event that just fired (a fact arrival or a resolution recording) could
    // satisfy any of them; per-step Evaluate inside TryFireDeferredCompletion makes the determination.
    TArray<FGameplayTag> StepTags;
    DeferredCompletions.GetKeys(StepTags);
    for (const FGameplayTag& StepTag : StepTags)
    {
        TryFireDeferredCompletion(StepTag);
    }
}

void UQuestManagerSubsystem::TryFireDeferredCompletion(FGameplayTag StepTag)
{
    TObjectPtr<UQuestNodeBase>* NodePtr = LoadedNodeInstances.Find(StepTag.GetTagName());
    if (!NodePtr) return;

    UQuestStep* Step = Cast<UQuestStep>(*NodePtr);
    if (!Step || !Step->PrerequisiteExpression.Evaluate(WorldState, QuestStateSubsystem)) return;

    UE_LOG(LogSimpleQuestActivation, Log, TEXT("TryFireDeferredCompletion: '%s' - prereqs satisfied, resuming chain"), *StepTag.ToString());

    // Clean up subscriptions
    if (TMap<FGameplayTag, FPrereqLeafSubscription::FPrereqLeafHandles>* Handles = DeferredCompletionPrereqHandles.Find(StepTag))
    {
        FPrereqLeafSubscription::UnsubscribeAll(QuestSignalSubsystem, *Handles);
        DeferredCompletionPrereqHandles.Remove(StepTag);
    }

    FQuestDeferredCompletion Pending;
    DeferredCompletions.RemoveAndCopyValue(StepTag, Pending);

    // Mint the cascade event ID at the deferred fire moment - that's when the gameplay event actually happens
    // in player-perceptible time (the original completion was deferred until prereqs satisfied). Mirrors
    // HandleOnNodeCompleted's minting pattern.
    FOriginatingEventID OriginatingEventID;
    OriginatingEventID.AuthoredNodeGuid = Step->GetAuthoredNodeGuid();
    OriginatingEventID.ResolutionTimestamp = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;

    ChainToNextNodes(Step, Pending.OutcomeTag, Pending.PathIdentity, OriginatingEventID);
}

void UQuestManagerSubsystem::HandleGiverRegisteredEvent(FGameplayTag Channel, const FQuestGiverRegisteredEvent& Event)
{
    const FGameplayTag QuestTag = Event.GetQuestTag();
    if (!QuestTag.IsValid()) return;

    RegisteredGiverQuestTags.Add(QuestTag);
    UE_LOG(LogSimpleQuestActivation, Verbose, TEXT("UQuestManagerSubsystem::HandleGiverRegisteredEvent : giver registered for '%s'"), *QuestTag.ToString());

    if (FQuestLifecycleQuery::IsLive(WorldState, QuestTag))
    {
        UE_LOG(LogSimpleQuestActivation, Warning,
            TEXT("UQuestManagerSubsystem::HandleGiverRegisteredEvent : giver for '%s' registered after the quest was already Live - gate not applied. Placed givers gate correctly via their InitializeComponent declaration; this is a runtime-spawned or late-streamed giver, which can't be gated retroactively."),
            *QuestTag.ToString());
    }
}

void UQuestManagerSubsystem::HandleNodeDeactivationRequest(FGameplayTag Channel, const FQuestDeactivateRequestEvent& Event)
{
    // Resolve input to canonical so SetQuestDeactivated's IsActiveLifecycle / fact-write paths target the
    // perspective state facts were actually written under. BP callers (DeactivateQuest helper) may pass any
    // perspective form; state lives at the canonical ContextualTag of the underlying instance.
    const FGameplayTag CanonicalTag = ResolveSingleCanonicalForMutation(Event.GetQuestTag());
    if (CanonicalTag.IsValid()) SetQuestDeactivated(CanonicalTag, Event.Source, Event.Context);
}

int32 UQuestManagerSubsystem::GetQuestCompletionCount(FGameplayTag QuestTag) const
{
    if (const UGameInstance* GI = GetGameInstance())
    {
        if (const UQuestStateSubsystem* ResolutionSubsystem = GI->GetSubsystem<UQuestStateSubsystem>())
        {
            return ResolutionSubsystem->GetResolutionCount(QuestTag);
        }
    }
    return 0;
}

void UQuestManagerSubsystem::SetQuestLive(FGameplayTag QuestTag)
{
    if (!WorldState || !QuestTag.IsValid()) return;

    // Idempotency guard at canonical perspective: symmetric with SetQuestBlocked / SetQuestDeactivated. Fan-in
    // convergence on a single quest (two upstream paths activating the same node, especially when both arrive while
    // an inner prereq defers) calls through here once per cascade and would otherwise bump the WorldState ref-count
    // past 1. Canonical-only guard is sufficient because all perspectives' ref-counts move in lockstep through the
    // multi-perspective Add/Remove helpers.
    if (FQuestLifecycleQuery::IsLive(WorldState, QuestTag))
    {
        UE_LOG(LogSimpleQuestActivation, Verbose, TEXT("SetQuestLive: '%s' already live, skipping (convergence)"), *QuestTag.ToString());
        return;
    }

    UE_LOG(LogSimpleQuestActivation, Verbose, TEXT("SetQuestLive: '%s'"), *QuestTag.ToString());
    AddStateFactAcrossPerspectives(QuestTag, EQuestStateLeaf::Live);
    MarkQuestStarted(QuestTag);

    // Ancestor walk for Steps. Containers' Live state is derived from inner Step state, so a Step
    // transitioning to Live propagates upward: each ancestor container re-derives its Live fact based on whether
    // any of its inner Steps is now Live. Innermost-first ordering means the immediate parent derives first;
    // outer ancestors derive against the now-up-to-date inner state. Non-Step callers (containers passed in by
    // misuse, future node kinds) skip the walk - there's no ancestor chain to traverse for them.
    if (UQuestNodeBase* Node = LoadedNodeInstances.FindRef(QuestTag.GetTagName()))
    {
        if (Node->IsStepNode())
        {
            DeriveAllAncestorContainersForStep(Cast<UQuestStep>(Node));
        }
    }
}

void UQuestManagerSubsystem::DeriveContainerLive(FGameplayTag ContainerTag)
{
    if (!WorldState || !ContainerTag.IsValid()) return;

    UQuest* Container = Cast<UQuest>(LoadedNodeInstances.FindRef(ContainerTag.GetTagName()));
    if (!Container) return;  // not a container - nothing to derive

    // A container is Live whenever any inner Step at any depth has an active lifecycle (Live or PendingGiver) -
    // a giver-gated inner Step keeps its container visibly in-progress because the player can interact with the
    // giver to advance. InnerStepTags is compile-time populated by ComputeContainerReachability and bounded by
    // the number of Steps inside this wrapper (typically a handful), so the linear scan is cheap. Short-circuits
    // on the first active inner Step.
    //
    // InnerStepTags reflect the WRAPPER's compile perspective, which may differ from the canonical perspective
    // post-AuthoredGuid dedup (the wrapper unique to one asset references inlined Steps whose canonicals were
    // registered first by another asset). HasActiveLifecycle is a direct WorldState->HasFact probe with no alias
    // walk, so without canonicalizing the query tag, an outer-asset wrapper sees its inner Steps as inactive
    // even when the canonical Live fact is set. ResolveToCanonicalTag resolves each entry to the perspective
    // queries hit.
    bool bAnyInnerLive = false;
    for (const FGameplayTag& InnerStepTag : Container->GetInnerStepTags())
    {
        const FGameplayTag CanonicalInner = ResolveToCanonicalTag(InnerStepTag);
        if (FQuestLifecycleQuery::HasActiveLifecycle(WorldState, CanonicalInner))
        {
            bAnyInnerLive = true;
            break;
        }
    }

    const FGameplayTag ContainerLiveFact = FQuestTagComposer::ResolveStateFactTag(ContainerTag, EQuestStateLeaf::Live);
    if (!ContainerLiveFact.IsValid()) return;

    const bool bCurrentlyLive = FQuestLifecycleQuery::IsLive(WorldState, ContainerTag);
    if (bAnyInnerLive && !bCurrentlyLive)
    {
        // Across perspectives, like every other lifecycle write: a container queried through an alias spelling must
        // read Live the same as through its canonical, or observers bound on that perspective never see it start.
        AddStateFactAcrossPerspectives(ContainerTag, EQuestStateLeaf::Live);
        MarkQuestStarted(ContainerTag);
        UE_LOG(LogSimpleQuestActivation, Verbose, TEXT("DeriveContainerLive: '%s' → Live (inner Step now active)"), *ContainerTag.ToString());
    }
    else if (!bAnyInnerLive && bCurrentlyLive)
    {
        RemoveStateFactAcrossPerspectives(ContainerTag, EQuestStateLeaf::Live);
        // ResolvedByEvents is intentionally NOT cleared here. Under multi-tag fanout, a single logical gameplay
        // event can produce two sequential cascades (one per per-context Step), and the first cascade's resolution
        // causes this branch to fire mid-event - clearing the set here would empty it before the second cascade
        // arrives at the wrapper, defeating the gate. The set is kept bounded by the prune-on-add logic in
        // FireWrapperBoundaryCompletion's gate (entries with strictly earlier timestamps are pruned when a
        // new event lands), so growth is naturally limited to events from the current tick.
        UE_LOG(LogSimpleQuestActivation, Verbose,
            TEXT("DeriveContainerLive: '%s' → not Live (no inner Steps active)"),
            *ContainerTag.ToString());
    }
    // else: container's Live state already matches what's derived - no action needed.
}

void UQuestManagerSubsystem::DeriveAllAncestorContainersForStep(UQuestStep* Step)
{
    if (!Step) return;

    // Pass 1: own-compile-perspective ancestors. The canonical Step's compile data covers wrappers in the
    // asset that registered this instance first.
    for (const FGameplayTag& AncestorTag : Step->GetAncestorContainerTags())
    {
        DeriveContainerLive(AncestorTag);
    }

    // Pass 2: foreign-compile-perspective ancestors derived from each alias's parent prefix chain. Bounded
    // by IsContainerTag so non-wrapper prefixes (asset roots, "SimpleQuest.Questline") are skipped without
    // requiring a runtime check at every prefix level.
    if (!QuestStateSubsystem) return;
    for (const FGameplayTag& AliasTag : Step->GetAssetScopedAliasTags())
    {
        if (!AliasTag.IsValid()) continue;
        FGameplayTag PrefixTag = AliasTag.RequestDirectParent();
        while (PrefixTag.IsValid())
        {
            if (QuestStateSubsystem->IsContainerTag(PrefixTag))
            {
                DeriveContainerLive(PrefixTag);
            }
            PrefixTag = PrefixTag.RequestDirectParent();
        }
    }
}

void UQuestManagerSubsystem::SetQuestResolved(FGameplayTag QuestTag, FGameplayTag OutcomeTag, FName PathIdentity, EQuestResolutionSource Source, const
                                              FOriginatingEventID& OriginatingEventID)
{
    if (!WorldState || !QuestTag.IsValid()) return;

    // Layer 1: WorldState boolean-fact layer. State facts are semantically boolean ("has X been asserted?") so each
    // AddFact is guarded against ref-count duplication on convergent or repeat-resolution paths. The resolution
    // registry below (Layer 2) and any downstream chain dispatch driven by the caller are NOT gated here. Quests
    // are allowed to resolve multiple times (stays-Live-after-completion, or deactivate > reactivate > re-resolve), and
    // each fire should append to history and propagate signals normally.
    //
    // Only Steps own a direct Live fact, so only Steps clear it here. For containers, Live is derived from inner
    // Step state by DeriveContainerLive; the container's Live transitions to false naturally when the last inner Step
    // transitions out of Live (Phase 5 wires that path symmetrically on SetQuestDeactivated and the Step-side resolution).
    // Skipping the RemoveFact here for containers also gives loopable wrappers correct semantics - the wrapper stays Live
    // across loop iterations as long as inner Steps remain Live, instead of flickering false on each outer outcome's chain
    // processing.
    UQuestNodeBase* Node = LoadedNodeInstances.FindRef(QuestTag.GetTagName());
    const bool bIsContainer = Node && Node->IsContainerNode();

    if (!bIsContainer)
    {
        RemoveStateFactAcrossPerspectives(QuestTag, EQuestStateLeaf::Live);

        // Step's Live just transitioned out, so its ancestor containers re-derive their Live state. Containers
        // skip this branch (they don't own a Live fact directly; their Live tracks inner Step state via the Step-side
        // ancestor walks). Wrappers being resolved as part of chain boundary processing route through this method but
        // with bIsContainer=true and don't trigger derivation.
        if (Node && Node->IsStepNode())
        {
            DeriveAllAncestorContainersForStep(Cast<UQuestStep>(Node));
        }
    }
    RemoveStateFactAcrossPerspectives(QuestTag, EQuestStateLeaf::PendingGiver);

    // Each SetQuestResolved call appends to the resolution registry (Layer 2 below) and bumps the WorldState
    // Completed fact's ref-count. Pre-multi-resolution this site guarded against double-add via an !IsCompleted
    // check - that guard predates the explicit "quests resolve multiple times" semantic documented in the layer
    // comment above and erased the per-resolution count the ref-count is meant to carry. Removing the guard
    // restores the count semantic: ref-count == number of resolutions in the current session for this canonical
    // (matches Layer 2's resolution record count).
    AddStateFactAcrossPerspectives(QuestTag, EQuestStateLeaf::Completed);

    // The blanket path-fact-to-WorldState write was removed in the Outcome/Path data-layer migration - the
    // resolution registry (UQuestStateSubsystem, Layer 2 below) is the canonical, append-only source of truth for
    // outcome/path queries (HasResolvedWith / HasResolvedAtPath), and subscribers watch FQuestResolutionRecorded-
    // Event on the ContextualTag channel (see RegisterEnablementWatch / DeferChainToNextNodes).
    //
    // Re-introduced NARROWLY here as the per-run resettable mirror: when the resolving node is resettable-replay
    // scoped, project this path resolution to a clearable WorldState fact - the same MakeNodePathFact tag pin-wired
    // prereqs carry - so a replay reset can clear it and the gate re-gates honestly. The registry record is never
    // touched by a reset; only this projection is. Non-resettable nodes keep the registry-only behavior.
    if (Node && Node->IsResettableReplay() && !PathIdentity.IsNone())
    {
        AddPathFactAcrossPerspectives(QuestTag, PathIdentity, OriginatingEventID);
    }

    // Layer 2: rich-record registry. Friend access only; external code can't mutate the registry,
    // but the manager writes it atomically with its own fact updates so the two layers stay consistent.
    // Append-only history: every resolution call records, supporting multi-outcome lifetimes per quest.
    if (UGameInstance* GI = GetGameInstance())
    {
        if (UQuestStateSubsystem* Registry = GI->GetSubsystem<UQuestStateSubsystem>())
        {
            const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
            Registry->RecordResolution(QuestTag, OutcomeTag, PathIdentity, Now, Source, OriginatingEventID);
        }
    }

    UE_LOG(LogSimpleQuestActivation, Log, TEXT("SetQuestResolved: '%s' outcome='%s' source=%s"),
        *QuestTag.ToString(),
        *OutcomeTag.ToString(),
        Source == EQuestResolutionSource::External ? TEXT("External") : TEXT("Graph"));
}

void UQuestManagerSubsystem::SetQuestPendingGiver(FGameplayTag QuestTag)
{
    if (!WorldState || !QuestTag.IsValid()) return;

    // Idempotency guard at canonical perspective, symmetric with SetQuestLive / SetQuestBlocked /
    // SetQuestDeactivated. The ActivateNodeByTag short-circuit already intercepts a second cascade arriving
    // while PendingGiver is asserted, so this is belt-and-braces; keeping the Set* methods uniformly idempotent
    // means any future caller reaching here can't accidentally bump the ref-count past 1 on a state that's
    // semantically a boolean.
    if (FQuestLifecycleQuery::IsPendingGiver(WorldState, QuestTag))
    {
        UE_LOG(LogSimpleQuestActivation, Verbose, TEXT("SetQuestPendingGiver: '%s' already pending, skipping"), *QuestTag.ToString());
        return;
    }

    AddStateFactAcrossPerspectives(QuestTag, EQuestStateLeaf::PendingGiver);
    UE_LOG(LogSimpleQuestActivation, Verbose, TEXT("SetQuestPendingGiver: '%s'"), *QuestTag.ToString());

    // Ancestor walk for Steps. Container Live derives off any-inner-step-active (Live or PendingGiver), so a Step
    // entering PendingGiver propagates upward: each ancestor container re-derives its Live fact based on whether
    // any inner Step is now active. Mirrors SetQuestLive's pattern. Without this walk, a giver-gated inner Step's
    // ancestor containers stay incorrectly inactive in debug surfaces (halos, prereq examiner) until a Step
    // transitions all the way to Live. Containers don't reach here themselves; they don't own a PendingGiver fact.
    if (UQuestNodeBase* Node = LoadedNodeInstances.FindRef(QuestTag.GetTagName()))
    {
        if (Node->IsStepNode())
        {
            DeriveAllAncestorContainersForStep(Cast<UQuestStep>(Node));
        }
    }
}

void UQuestManagerSubsystem::ClearQuestPendingGiver(FGameplayTag QuestTag)
{
    if (WorldState && QuestTag.IsValid())
    {
        RemoveStateFactAcrossPerspectives(QuestTag, EQuestStateLeaf::PendingGiver);
        UE_LOG(LogSimpleQuestActivation, Verbose, TEXT("ClearQuestPendingGiver: '%s'"), *QuestTag.ToString());
    }
}

void UQuestManagerSubsystem::FireWrapperBoundaryCompletion(const FQuestBoundaryCompletion& BC, const FOriginatingEventID& OriginatingEventID, const FQuestObjectiveActivationContext& InheritedForward)
{
    const FGameplayTag WrapperTag = UGameplayTagsManager::Get().RequestGameplayTag(BC.WrapperTagName, false);
    if (!WrapperTag.IsValid()) return;

    if (UQuestNodeBase* WrapperNode = LoadedNodeInstances.FindRef(BC.WrapperTagName))
    {
        // Event-keyed dedup gate: a single gameplay event (Step resolution → cascade → wrapper completion)
        // can reach the same wrapper through multiple paths under multi-tag fanout - e.g., both this
        // context's Listener and another context's Listener forwarding their BoundaryCompletions to this
        // wrapper after their respective Setters publish on the shared GroupTag channel. Without this gate,
        // the wrapper resolves once per arriving cascade, doubling (or N-tupling) records for one logical event.
        // With the gate, the second arrival with a matching event ID is recognized as already-handled and
        // skipped; loops that re-resolve at a later moment (different timestamp) or multi-resolution within
        // a single Live phase from a different originating Step (different authored GUID) produce distinct
        // event IDs and proceed normally. Invalid event IDs (default-constructed - non-cascade origin like
        // direct external API resolution) skip the dedup logic entirely so non-cascade paths aren't filtered.
        if (UQuest* WrapperContainer = Cast<UQuest>(WrapperNode); WrapperContainer && OriginatingEventID.IsValid())
        {
            if (WrapperContainer->ResolvedByEvents.Contains(OriginatingEventID))
            {
                UE_LOG(LogSimpleQuestActivation, Verbose,
                    TEXT("FireWrapperBoundaryCompletion: skipping '%s' outcome='%s' - already resolved by event guid=%s ts=%.3f"),
                    *WrapperTag.ToString(), *BC.OutcomeTag.ToString(),
                    *OriginatingEventID.AuthoredNodeGuid.ToString(EGuidFormats::Short),
                    OriginatingEventID.ResolutionTimestamp);
                return;
            }
            
            // Prune entries with strictly earlier timestamps before adding the new one. Within one logical
            // event's multi-tag fanout every cascade shares the same ResolutionTimestamp; entries with older
            // timestamps belong to events from prior ticks that cannot recur (the same authored Step at the
            // same world time would produce an identical entry already in the set, caught by the Contains
            // check above). Keeps the set bounded to events from the current tick - typically 1 entry,
            // occasionally a few for multi-resolution scenarios. No unbounded growth across session length.
            for (auto It = WrapperContainer->ResolvedByEvents.CreateIterator(); It; ++It)
            {
                if (It->ResolutionTimestamp < OriginatingEventID.ResolutionTimestamp)
                {
                    It.RemoveCurrent();
                }
            }
            WrapperContainer->ResolvedByEvents.Add(OriginatingEventID);
        }

        UE_LOG(LogSimpleQuestActivation, Verbose,
            TEXT("FireWrapperBoundaryCompletion: routing '%s' outcome='%s' through wrapper's ChainToNextNodes (eventGuid=%s)"),
            *WrapperTag.ToString(), *BC.OutcomeTag.ToString(),
            *OriginatingEventID.AuthoredNodeGuid.ToString(EGuidFormats::Short));
        ChainToNextNodes(WrapperNode, BC.OutcomeTag, BC.OutcomeTag.GetTagName(), OriginatingEventID, InheritedForward);
    }
    else
    {
        UE_LOG(LogSimpleQuestActivation, Warning,
            TEXT("FireWrapperBoundaryCompletion: wrapper '%s' instance not loaded - falling back to direct SetQuestResolved + publish"),
            *WrapperTag.ToString());
        SetQuestResolved(WrapperTag, BC.OutcomeTag, NAME_None, EQuestResolutionSource::Graph, OriginatingEventID);
    }
}

void UQuestManagerSubsystem::PublishGraphResolutions(const TArray<FQuestGraphResolution>& Resolutions, EQuestResolutionSource Source, const FQuestObjectiveActivationContext
                                                     & CompleterContext)
{
    if (Resolutions.IsEmpty()) return;

    UQuestStateSubsystem* StateSubsystem = GetGameInstance() ? GetGameInstance()->GetSubsystem<UQuestStateSubsystem>() : nullptr;
    if (!StateSubsystem) return;

    const double ResolutionTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
    for (const FQuestGraphResolution& Resolution : Resolutions)
    {
        if (!Resolution.GraphTag.IsValid() || !Resolution.OutcomeTag.IsValid()) continue;

        UE_LOG(LogSimpleQuestActivation, Verbose, TEXT("PublishGraphResolutions: '%s' outcome='%s'"),
            *Resolution.GraphTag.ToString(),
            *Resolution.OutcomeTag.ToString());

        // QSV layer: rich-record registry entry using the Exit's authored OutcomeTag (what the questline
        // resolves WITH), not any upstream cascading path outcome.
        StateSubsystem->RecordResolution(Resolution.GraphTag, Resolution.OutcomeTag, NAME_None, ResolutionTime, Source);

        // WSV layer: Completed fact write at the asset identity. Asset identities aren't aliased in the
        // current compile model, but AddStateFactAcrossPerspectives handles single-canonical uniformly.
        AddStateFactAcrossPerspectives(Resolution.GraphTag, EQuestStateLeaf::Completed);

        // Bus publish at the questline asset's tag channel so questline-tag subscribers (Hierarchical or
        // ExactMatch) receive a direct questline-level Ended event. Closes the gap that previously forced
        // questline-tag observers to rely on ancestor-walk delivery from Step-tag publishes (which the
        // ExactMatch routing mode correctly filters out). Payload is minimal - questline-level resolutions
        // don't have a specific completing node to attribute beyond the asset identity itself; adopters
        // who need per-Step context subscribe to Step-tag FQuestEndedEvent (still published via the
        // PublishQuestEndedEvent path on the completing Step's channel).
        if (QuestSignalSubsystem)
        {
            FQuestEventPayload Payload;
            Payload.NodeInfo.QuestTag = Resolution.GraphTag;
            QuestSignalSubsystem->PublishMessage<FQuestEndedEvent>(
                Resolution.GraphTag,
                FQuestEndedEvent(Resolution.GraphTag, Resolution.OutcomeTag, Source, Payload));
        }
        
        // Questline-level rewards: read the questline's compiled reward map by its identity tag and grant, for this
        // completion, BOTH the set keyed to the resolved outcome AND the Any-Outcome set (which fires regardless of
        // which outcome resolved - the resolution never carries the Any-Outcome tag itself, so it must be consulted
        // explicitly, mirroring how a Grant Rewards node on the Any-Outcome pin fires on every path). Completer forward
        // attribution matches a wired reward node's. The map is keyed by identity FName and flattened from every
        // registered graph's CompiledQuestlineRewards, so a linked questline's rewards resolve here without its source
        // asset (never loaded at runtime) - the one place a questline's whole-questline rewards fire, standalone or embedded.
        if (const FQuestCompiledQuestlineRewards* Compiled = LiveQuestlineRewardsByIdentity.Find(Resolution.GraphTag.GetTagName()))
        {
            FQuestRewardActivationContext RewardIncoming;
            static_cast<FQuestContextBase&>(RewardIncoming) = CompleterContext;
            RewardIncoming.IncomingOutcomeTag = Resolution.OutcomeTag;

            if (const FQuestRewardSet* Set = Compiled->RewardsByOutcome.Find(Resolution.OutcomeTag); Set && !Set->Rewards.IsEmpty())
            {
                UQuestRewardNode::GrantRewardSet(Set->Rewards, RewardIncoming, QuestSignalSubsystem);
            }
            if (const FQuestRewardSet* AnySet = Compiled->RewardsByOutcome.Find(TAG_Outcome_AnyOutcome.GetTag()); AnySet && !AnySet->Rewards.IsEmpty())
            {
                UQuestRewardNode::GrantRewardSet(AnySet->Rewards, RewardIncoming, QuestSignalSubsystem);
            }
        }
    }
}

void UQuestManagerSubsystem::RegisterEnablementWatch(FGameplayTag QuestTag, FName NodeTagName, const FPrerequisiteExpression& Expr, bool bInitialSatisfied)
{
    if (!QuestSignalSubsystem) return;

    FEnablementWatch& Watch = EnablementWatches.FindOrAdd(QuestTag);
    Watch.NodeTagName = NodeTagName;
    Watch.bLastKnownSatisfied = bInitialSatisfied;

    TMap<FGameplayTag, FPrereqLeafSubscription::FPrereqLeafHandles>& Handles = EnablementWatchHandles.FindOrAdd(QuestTag);
    FPrereqLeafSubscription::SubscribeLeavesForReevaluation(
        Expr,
        this,
        &UQuestManagerSubsystem::OnEnablementLeafFactAdded,
        &UQuestManagerSubsystem::OnEnablementLeafFactRemoved,
        &UQuestManagerSubsystem::OnEnablementLeafResolutionRecorded,
        &UQuestManagerSubsystem::OnEnablementLeafEntryRecorded,
        Handles);

    UE_LOG(LogSimpleQuestSubscription, Verbose, TEXT("RegisterEnablementWatch: '%s' subscribed to %d channel(s), initial satisfied=%d"),
        *QuestTag.ToString(),
        Handles.Num(),
        bInitialSatisfied ? 1 : 0);
}

void UQuestManagerSubsystem::OnEnablementLeafFactAdded(FGameplayTag Channel, const FWorldStateFactAddedEvent& Event)
{
    ReevaluateAllEnablementWatches();
}

void UQuestManagerSubsystem::OnEnablementLeafFactRemoved(FGameplayTag Channel, const FWorldStateFactRemovedEvent& Event)
{
    ReevaluateAllEnablementWatches();
}

void UQuestManagerSubsystem::OnEnablementLeafResolutionRecorded(FGameplayTag Channel, const FQuestResolutionRecordedEvent& Event)
{
    ReevaluateAllEnablementWatches();
}

void UQuestManagerSubsystem::OnEnablementLeafEntryRecorded(FGameplayTag Channel, const FQuestEntryRecordedEvent& Event)
{
    ReevaluateAllEnablementWatches();
}

void UQuestManagerSubsystem::ReevaluateAllEnablementWatches()
{
    // Iterate every active watch; each re-evaluation is cheap and avoids needing an inverse channel-to-watch
    // map. Called from all three OnEnablementLeaf*** handlers (FactAdded / FactRemoved / ResolutionRecorded).
    TArray<FGameplayTag> Keys;
    EnablementWatches.GetKeys(Keys);
    for (const FGameplayTag& QuestTag : Keys)
    {
        ReevaluateEnablementWatch(QuestTag);
    }
}

void UQuestManagerSubsystem::ReevaluateEnablementWatch(FGameplayTag QuestTag)
{
    FEnablementWatch* Watch = EnablementWatches.Find(QuestTag);
    if (!Watch) return;

    UQuestNodeBase* Instance = LoadedNodeInstances.FindRef(Watch->NodeTagName);
    if (!Instance) return;

    // Compute full status (with leaf detail) - we both push it to the state subsystem and use the bSatisfied
    // bit for the transition check.
    const FQuestPrereqStatus NewStatus = Instance->PrerequisiteExpression.EvaluateWithLeafStatus(WorldState, QuestStateSubsystem);

    // Push to state subsystem regardless of transition - the cache should always reflect current evaluation
    // even on no-transition leaf changes (e.g., a NOT clause's leaf flipping when the overall result happens
    // to stay the same).
    if (UQuestStateSubsystem* StateSubsystem = GetGameInstance()
        ? GetGameInstance()->GetSubsystem<UQuestStateSubsystem>() : nullptr)
    {
        StateSubsystem->UpdateQuestPrereqStatus(QuestTag, NewStatus);
    }

    if (NewStatus.bSatisfied == Watch->bLastKnownSatisfied) return;  // no transition

    Watch->bLastKnownSatisfied = NewStatus.bSatisfied;

    if (!QuestSignalSubsystem) return;
    FQuestEventPayload Context = AssembleEventContext(Instance, FQuestObjectiveTriggerContext());

    if (NewStatus.bSatisfied)
    {
        UE_LOG(LogSimpleQuestSubscription, Log, TEXT("ReevaluateEnablementWatch: '%s' - prereqs satisfied, publishing Enabled"),
            *QuestTag.ToString());
        FQuestPublish::OnAllNodeTags(QuestSignalSubsystem, Instance, FQuestEnabledEvent(QuestTag, Context));
    }
    else
    {
        UE_LOG(LogSimpleQuestSubscription, Log, TEXT("ReevaluateEnablementWatch: '%s' - prereqs no longer satisfied, publishing Disabled"),
            *QuestTag.ToString());
        FQuestPublish::OnAllNodeTags(QuestSignalSubsystem, Instance, FQuestDisabledEvent(QuestTag, Context));
    }
}

void UQuestManagerSubsystem::ClearEnablementWatch(FGameplayTag QuestTag)
{
    if (TMap<FGameplayTag, FPrereqLeafSubscription::FPrereqLeafHandles>* Handles = EnablementWatchHandles.Find(QuestTag))
    {
        FPrereqLeafSubscription::UnsubscribeAll(QuestSignalSubsystem, *Handles);
        EnablementWatchHandles.Remove(QuestTag);
    }
    EnablementWatches.Remove(QuestTag);

    // Clear cached prereq status. The quest is leaving giver state, the cache entry is no longer relevant.
    if (UQuestStateSubsystem* StateSubsystem = GetGameInstance() ? GetGameInstance()->GetSubsystem<UQuestStateSubsystem>() : nullptr)
    {
        StateSubsystem->ClearQuestPrereqStatus(QuestTag);
    }
}
