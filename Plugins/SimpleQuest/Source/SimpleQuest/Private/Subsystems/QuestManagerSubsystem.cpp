// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Subsystems/QuestManagerSubsystem.h"
#include "Quests/QuestlineGraph.h"
#include "Quests/QuestNodeBase.h"
#include "Signals/SignalSubsystem.h"
#include "GameplayTagsManager.h"
#include "SimpleQuestLog.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Events/QuestEndedEvent.h"
#include "Events/QuestObjectiveTriggered.h"
#include "Events/QuestProgressEvent.h"
#include "Events/QuestStartedEvent.h"
#include "Events/QuestEnabledEvent.h"
#include "Events/QuestDeactivatedEvent.h"
#include "Events/QuestActivatedEvent.h"
#include "Events/QuestActivationRequestEvent.h"
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
#include "Objectives/QuestObjective.h"
#include "Quests/Quest.h"
#include "Quests/QuestStep.h"
#include "WorldState/WorldStateSubsystem.h"
#include "Quests/Types/QuestEventContext.h"
#include "Quests/Types/QuestObjectiveContext.h"
#include "Settings/SimpleQuestSettings.h"
#include "StructUtils/InstancedStruct.h"
#include "Subsystems/QuestStateSubsystem.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Quests/Types/PrereqLeafSubscription.h"
#include "Utilities/QuestTagComposer.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Kismet/KismetMathLibrary.h"
#include "Utilities/QuestActivationGuard.h"
#include "Utilities/QuestLifecycleQuery.h"
#include "Utilities/QuestLifecycleQuery.h"
#include "Utilities/QuestPublish.h"
#if WITH_EDITOR
#include "Components/QuestGiverComponent.h"
#endif

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
        }
    }

    RegisterGiversFromAssetRegistry();
    
    UE_LOG(LogSimpleQuest, Log, TEXT("UQuestManagerSubsystem::Initialize : Initializing: %s"), *GetFullName());

    // Bring all listener-bearing graphs online before any StartQuestline caller can publish a setter event. AR is
    // typically loaded by the time game-instance subsystems initialize (editor PIE always; cooked starts almost
    // always — cooked AR is serialized as a single early-loaded blob). The OnFilesLoaded fallback covers the
    // narrow cooked-cold-start window where AR is still streaming when this fires.
    IAssetRegistry& AR = FAssetRegistryModule::GetRegistry();
    if (AR.IsLoadingAssets())
    {
        AR.OnFilesLoaded().AddUObject(this, &UQuestManagerSubsystem::AutoLoadListenerBearingGraphs);
    }
    else
    {
        AutoLoadListenerBearingGraphs();
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

    // No designated class in settings — fall back to the base class only, so the system still functions out of the box.
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
        QuestSignalSubsystem->UnsubscribeMessage(Tag_Channel_QuestTarget, ClassBridgeHandle);
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
    Super::Deinitialize();
}

void UQuestManagerSubsystem::CheckQuestObjectives(FGameplayTag Channel, const FInstancedStruct& RawEvent)
{
    const FQuestObjectiveTriggered* Event = RawEvent.GetPtr<FQuestObjectiveTriggered>();
    if (!Event) return;

    TObjectPtr<UQuestNodeBase>* NodePtr = LoadedNodeInstances.Find(Channel.GetTagName());
    if (!NodePtr) return;

    UQuestStep* Step = Cast<UQuestStep>(*NodePtr);
    if (!Step || !Step->GetLiveObjective()) return;

    if (Step->GetPrerequisiteGateMode() == EPrerequisiteGateMode::GatesProgression
        && !Step->IsGiverGated()
        && !Step->PrerequisiteExpression.IsAlways())
    {
        if (!Step->PrerequisiteExpression.Evaluate(WorldState, QuestStateSubsystem)) return;
    }

    FQuestObjectiveContext Context;
    Context.TriggeredActor = Cast<AActor>(Event->TriggeredActor);
    Context.Instigator = Cast<AActor>(Event->Instigator);
    Context.CustomData = Event->CustomData;
    Step->GetLiveObjective()->DispatchTryCompleteObjective(Context);
}

void UQuestManagerSubsystem::RegisterQuestlineGraph(UQuestlineGraph* Graph)
{
    if (!Graph) return;

    int32 NewlyRegistered = 0;
    int32 SkippedAlreadyRegistered = 0;
    for (const auto& Pair : Graph->GetCompiledNodes())
    {
        if (UQuestNodeBase* Instance = Pair.Value)
        {
            // PrereqRule monitors are singleton-per-rule by design — the same RuleTagName key is emitted by every
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

            // Compiled node instances live on the UQuestlineGraph asset and persist across PIE sessions. Wipe any
            // state the prior session left on them — subscription handles to a dead SignalSubsystem, deferred
            // contextual tags, activation scratch, completion snapshots — so this session starts clean.
            Instance->ResetTransientState();

            if (!Pair.Key.ToString().StartsWith(TEXT("Util_")))
            {
                Instance->ResolveContextualTag(Pair.Key);
            }
            LoadedNodeInstances.Add(Pair.Key, Instance);
            Instance->RegisterWithGameInstance(GetGameInstance());
            Instance->OnRegisteredWithManager();
            Instance->OnNodeCompleted.BindDynamic(this, &UQuestManagerSubsystem::HandleOnNodeCompleted);
            Instance->OnNodeStarted.BindDynamic(this, &UQuestManagerSubsystem::HandleOnNodeStarted);
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
            }

            // Push structural info to the state subsystem. Single lookup serves both the KnownQuests registration
            // (every valid quest tag — answers GetQuestTagsUnderPrefix for hierarchical catch-up subscribers and
            // IsKnownQuestTag for runtime-instance presence) and the container classification (drives the blocker
            // query's AlreadyLive split). Mirrors the existing record-pushes pattern (RecordResolution / RecordEntry /
            // UpdateQuestPrereqStatus) — manager pushes structural info; state subsystem owns the public read surface.
            if (ResolvedTag.IsValid())
            {
                if (UQuestStateSubsystem* StateSubsystem = GetGameInstance() ? GetGameInstance()->GetSubsystem<UQuestStateSubsystem>() : nullptr)
                {
                    StateSubsystem->RegisterQuestTag(ResolvedTag);
                    if (Instance->IsContainerNode())
                    {
                        StateSubsystem->RegisterContainerTag(ResolvedTag);
                    }

                    // Push asset-scoped alias mappings so cross-asset subscribers can resolve through the alias
                    // index (read APIs alias-walk, RecordResolution / RecordEntry multi-publish). Empty for top-
                    // level content — the loop body skips when AssetScopedAliasTags is empty.
                    const TArray<FGameplayTag>& AliasesAtCallSite = Instance->GetAssetScopedAliasTags();
                    for (const FGameplayTag& AliasTag : AliasesAtCallSite)
                    {
                        StateSubsystem->RegisterAlias(AliasTag, ResolvedTag);
                    }
                }
            }
            ++NewlyRegistered;
        }
    }
    UE_LOG(LogSimpleQuest, Log, TEXT("RegisterQuestlineGraph: '%s' — registered %d new node instance(s); skipped %d already registered"),
        *Graph->GetName(),
        NewlyRegistered,
        SkippedAlreadyRegistered);
}

void UQuestManagerSubsystem::ActivateQuestlineGraph(UQuestlineGraph* Graph)
{
    if (!Graph) return;

    RegisterQuestlineGraph(Graph);

    UE_LOG(LogSimpleQuest, Log, TEXT("ActivateQuestlineGraph: '%s' — firing %d entry tag(s)"),
        *Graph->GetName(), Graph->GetEntryNodeTags().Num());

    for (const FName& EntryTagName : Graph->GetEntryNodeTags())
    {
        ActivateNodeByTag(EntryTagName, EQuestActivationProvenance::InitialEntry);
    }
}

FQuestEventContext UQuestManagerSubsystem::AssembleEventContext(const UQuestNodeBase* Node, const FQuestObjectiveContext& InCompletionContext) const
{
    FQuestEventContext Context;
    Context.NodeInfo = Node->GetNodeInfo();
    Context.CompletionContext = InCompletionContext;

    UE_LOG(LogSimpleQuest, Verbose, TEXT("AssembleEventContext: '%s' DisplayName='%s' CompletionContext=%s"),
        *Context.NodeInfo.QuestTag.ToString(),
        *Context.NodeInfo.DisplayName.ToString(),
        Context.CompletionContext.TriggeredActor ? TEXT("set") : TEXT("empty"));

    return Context;
}

void UQuestManagerSubsystem::HandleOnNodeCompleted(UQuestNodeBase* Node, FGameplayTag OutcomeTag, FName PathIdentity)
{
    UE_LOG(LogSimpleQuest, Log, TEXT("HandleOnNodeCompleted: '%s' outcome='%s' path='%s'"),
        *Node->GetContextualTag().ToString(), *OutcomeTag.ToString(), *PathIdentity.ToString());

    UQuestStep* Step = Cast<UQuestStep>(Node);
    if (Step
        && !Step->IsGiverGated()
        && Step->GetPrerequisiteGateMode() == EPrerequisiteGateMode::GatesCompletion
        && !Step->PrerequisiteExpression.IsAlways()
        && !Step->PrerequisiteExpression.Evaluate(WorldState, QuestStateSubsystem))
    {
        UE_LOG(LogSimpleQuest, Verbose, TEXT("HandleOnNodeCompleted: '%s' — prereqs unmet, deferring chain"), *Node->GetContextualTag().ToString());
        DeferChainToNextNodes(Step, OutcomeTag, PathIdentity);
        return;
    }

    // Mint the cascade event ID at the originating Step's completion. Multi-tag-stable AuthoredNodeGuid (same
    // authored Step in two compile contexts shares it) + per-tick-stable timestamp distinguishes cascades from
    // genuinely different gameplay events. Threaded through ChainToNextNodes onto every destination's
    // PendingActivationParams and into every FireWrapperBoundaryCompletion call.
    FOriginatingEventID OriginatingEventID;
    OriginatingEventID.AuthoredNodeGuid = Node->GetAuthoredNodeGuid();
    OriginatingEventID.ResolutionTimestamp = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
    UE_LOG(LogSimpleQuest, Verbose,
        TEXT("HandleOnNodeCompleted: '%s' minted cascade event ID — guid=%s ts=%.3f"),
        *Node->GetContextualTag().ToString(),
        *OriginatingEventID.AuthoredNodeGuid.ToString(EGuidFormats::Short),
        OriginatingEventID.ResolutionTimestamp);

    ChainToNextNodes(Node, OutcomeTag, PathIdentity, OriginatingEventID);
}

void UQuestManagerSubsystem::HandleOnNodeProgress(UQuestStep* Step, FQuestObjectiveContext ProgressData)
{
    if (!Step || !QuestSignalSubsystem) return;

    UE_LOG(LogSimpleQuest, Verbose, TEXT("HandleOnNodeProgress: '%s' — %d/%d"),
        *Step->GetContextualTag().ToString(),
        ProgressData.CurrentCount,
        ProgressData.RequiredCount);

    FQuestEventContext Context = AssembleEventContext(Step, ProgressData);
    FQuestPublish::OnAllNodeTags(QuestSignalSubsystem, Step, FQuestProgressEvent(Step->GetContextualTag(), Context));
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
        FQuestEventContext Context = AssembleEventContext(Node, FQuestObjectiveContext());
        AActor* GiverActor = nullptr;
        
        if (TWeakObjectPtr<AActor>* Found = RecentGiverActors.Find(Node->GetContextualTag()))
        {
            GiverActor = Found->Get();
            RecentGiverActors.Remove(Node->GetContextualTag());
        }

        // Suppress QuestStartedEvent for wrappers re-entering while already Live (loop-back wires, fan-in
        // re-entry, GiverGateSkipPathAware / ContainerReentry decisions). The event semantically means
        // "transitioned to Live" — firing it on a no-op re-entry tells subscribers a false transition
        // happened, breaking state machines that pair QuestStarted/QuestEnded as Live-cycle markers (the
        // giver component's container management is one such consumer). For real first-time activation,
        // the wrapper isn't yet Live at this point — the inner Step's SetQuestLive runs later in the same
        // call stack via DeriveContainerLive's ancestor walk — so the check below correctly publishes.
        // Steps don't need this guard: their RefuseStepAlreadyLive decision returns early in
        // ActivateNodeByTag, before reaching HandleOnNodeStarted at all. Re-entry awareness for designer
        // observation lives in the entry-record events (FQuestEntryRecordedEvent), which still fire below.
        const bool bIsWrapperReentry = Node->IsContainerNode() && FQuestLifecycleQuery::IsLive(WorldState, Node->GetContextualTag());
        if (!bIsWrapperReentry)
        {
            FQuestPublish::OnAllNodeTags(QuestSignalSubsystem, Node, FQuestStartedEvent(Node->GetContextualTag(), Context, GiverActor));
        }
        else
        {
            UE_LOG(LogSimpleQuest, Verbose,
                TEXT("HandleOnNodeStarted: '%s' re-entering while already Live — suppressing QuestStartedEvent (no-op re-activation)"),
                *Node->GetContextualTag().ToString());
        }
        
        if (UQuestStep* Step = Cast<UQuestStep>(Node))
        {
            FDelegateHandle Handle = QuestSignalSubsystem->SubscribeRawMessage<FQuestObjectiveTriggered>(Node->GetContextualTag(), this, &UQuestManagerSubsystem::CheckQuestObjectives);
            LiveStepTriggerHandles.Add(Node->GetContextualTag(), Handle);
            if (!Step->GetTargetClasses().IsEmpty())
            {
                for (const TSoftClassPtr<AActor>& SoftClass : Step->GetTargetClasses())
                {
                    // LoadSynchronous at step activation — pay the load cost once per target class when the step goes live,
                    // keep runtime event-dispatch checks fast by caching the loaded UClass in ClassFilteredSteps (TMultiMap<FGameplayTag, UClass*>).
                    if (UClass* Loaded = SoftClass.LoadSynchronous())
                    {
                        ClassFilteredSteps.Add(Node->GetContextualTag(), Loaded);
                    }
                }

                // Subscribe once to global channel if this is the first class-filtered step
                if (!ClassBridgeHandle.IsValid())
                {
                    ClassBridgeHandle = QuestSignalSubsystem->SubscribeRawMessage<FQuestObjectiveTriggered>(Tag_Channel_QuestTarget, this, &UQuestManagerSubsystem::CheckClassObjectives);
                }
            }

            // Step-side entry record. Captures every Step start with the merged final params snapshot delivered to the
            // live objective (Step->ReceivedActivationParams). Mirrors the wrapper-side per-cascade RecordEntry in the
            // UQuest branch below — wrapper records "this wrapper was entered by these cascades," Step records "this Step
            // was activated with these merged params." Together they cover the full registry surface the §1.2 historical-
            // context goal targets. SourceQuestTag / IncomingOutcomeTag come from the snapshot's cascade fields (invalid
            // for non-cascade-driven Step starts). PathIdentity is NAME_None because Steps don't have per-source routing.
            if (QuestStateSubsystem && Node->GetContextualTag().IsValid())
            {
                const FQuestObjectiveActivationParams& Snapshot = Step->GetReceivedActivationParams();
                const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
                QuestStateSubsystem->RecordEntry(
                    Node->GetContextualTag(),
                    Snapshot.OriginTag,
                    Snapshot.IncomingOutcomeTag,
                    Now,
                    Snapshot.Provenance,
                    Snapshot,
                    NAME_None);
                UE_LOG(LogSimpleQuest, Verbose, TEXT("HandleOnNodeStarted: recorded Step entry for '%s' provenance=%s giver='%s'"),
                    *Node->GetContextualTag().ToString(),
                    *UEnum::GetValueAsString(Snapshot.Provenance),
                    Snapshot.ActivationSource ? *Snapshot.ActivationSource->GetName() : TEXT("null"));
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
        // multiple — every cascade that arrived during the deferral window stamped its own snapshot. All entries
        // fire here so fan-in convergence patterns route correctly.
        TArray<FQuestObjectiveActivationParams> DrainedCascades;
        Swap(DrainedCascades, QuestNode->PendingEntryActivations);
        QuestNode->PendingActivationParams = FQuestObjectiveActivationParams{};

        // Defensive synthesis for paths that fire OnNodeStarted without going through ActivateNodeByTag's queue
        // append (e.g., direct Activate calls). Synthesizes one empty cascade so Any-Outcome entries still fire.
        if (DrainedCascades.IsEmpty())
        {
            DrainedCascades.Add(FQuestObjectiveActivationParams{});
        }

        // Use the first cascade's params for Any-Outcome entries (these fire ONCE per OnNodeStarted, not per
        // cascade — they're unconditional "Quest started" entries). Matches pre-queue behavior where the first
        // cascade's stamping won via diamond convergence on subsequent calls.
        const FQuestObjectiveActivationParams& FirstCascade = DrainedCascades[0];
        TArray<FGameplayTag> AnyOutcomeChain = FirstCascade.OriginChain;
        if (QuestNode->GetContextualTag().IsValid())
        {
            AnyOutcomeChain.Add(QuestNode->GetContextualTag());
        }

        auto StampWithParams = [this, &QuestNode](const FName& DestTagName,
            const FQuestObjectiveActivationParams& Params, const TArray<FGameplayTag>& Chain)
        {
            if (UQuestNodeBase* DestInstance = LoadedNodeInstances.FindRef(DestTagName))
            {
                DestInstance->PendingActivationParams = Params;
                DestInstance->PendingActivationParams.OriginTag = QuestNode->GetContextualTag();
                DestInstance->PendingActivationParams.OriginChain = Chain;
            }
        };

        // Always-activate Any-Outcome entries. Fire ONCE per OnNodeStarted, not per cascade.
        for (const FName& StepTag : QuestNode->GetEntryStepTags())
        {
            StampWithParams(StepTag, FirstCascade, AnyOutcomeChain);
            ActivateNodeByTag(StepTag, EQuestActivationProvenance::ChainCascade);
        }

        // Per-cascade outcome-specific routing. Each queued cascade fires its own entry routes — this is the
        // path that fan-in convergence patterns rely on (Q1's Victory and Q2's Defeat both routing into separate
        // inner steps when the Quest's prereq finally satisfies).
        for (const FQuestObjectiveActivationParams& CascadeParams : DrainedCascades)
        {
            const FGameplayTag IncomingOutcomeTag = CascadeParams.IncomingOutcomeTag;
            const FName IncomingSourceTag = CascadeParams.OriginTag.IsValid() ? CascadeParams.OriginTag.GetTagName() : NAME_None;

            // Record this cascade's per-source entry into the QuestStateSubsystem entry registry. Parallel to
            // the resolution registry pattern from item 2: appends an FQuestEntryArrival to the destination's
            // FQuestEntryRecord and broadcasts FQuestEntryRecordedEvent on the destination's tag channel.
            // Inner-graph Leaf_Entry prereqs subscribe to that event via FPrereqLeafSubscription and re-evaluate.
            if (IncomingOutcomeTag.IsValid() && QuestStateSubsystem && QuestNode->GetContextualTag().IsValid())
            {
                const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
                QuestStateSubsystem->RecordEntry(
                    QuestNode->GetContextualTag(),
                    CascadeParams.OriginTag,
                    IncomingOutcomeTag,
                    Now,
                    CascadeParams.Provenance,
                    CascadeParams,
                    IncomingSourceTag);
                UE_LOG(LogSimpleQuest, Verbose, TEXT("HandleOnNodeStarted: recorded entry for '%s' source='%s' outcome='%s' provenance=%s path='%s'"),
                    *QuestNode->GetContextualTag().ToString(),
                    *CascadeParams.OriginTag.ToString(),
                    *IncomingOutcomeTag.ToString(),
                    *UEnum::GetValueAsString(CascadeParams.Provenance),
                    *IncomingSourceTag.ToString());
            }

            // Build chain for this cascade.
            TArray<FGameplayTag> InnerForwardChain = CascadeParams.OriginChain;
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
                            StampWithParams(Entry.DestTag, CascadeParams, InnerForwardChain);
                            ActivateNodeByTag(Entry.DestTag, EQuestActivationProvenance::ChainCascade);
                        }
                    }
                }
            }

            // Any-outcome-from-source entries — bucket keyed by invalid FGameplayTag. Fires when the incoming
            // source matches, regardless of which specific outcome triggered entry.
            if (IncomingSourceTag != NAME_None)
            {
                if (const FQuestEntryRouteList* AnyRouteList = QuestNode->GetEntryStepTagsByPath().Find(NAME_None))
                {
                    for (const FQuestEntryDestination& Entry : AnyRouteList->Destinations)
                    {
                        if (Entry.SourceFilter == IncomingSourceTag)
                        {
                            StampWithParams(Entry.DestTag, CascadeParams, InnerForwardChain);
                            ActivateNodeByTag(Entry.DestTag, EQuestActivationProvenance::ChainCascade);
                        }
                    }
                }
            }
        }
    }
}

void UQuestManagerSubsystem::HandleOnNodeForwardActivated(UQuestNodeBase* Node)
{
    if (!Node) return;

    // The utility node's PendingActivationParams was populated by the upstream activation (cascade stamp, or
    // signal-driven self-stamp on UActivationGroupListenerNode). Its OriginatingEventID identifies the gameplay
    // event that drove the upstream cascade — pass it to FireWrapperBoundaryCompletion so the wrapper gate
    // sees the same identity that already-cascade-bearing destinations would. The wholesale
    // PendingActivationParams copy below carries OriginatingEventID onto downstream destinations naturally.
    const FOriginatingEventID& InheritedEventID = Node->PendingActivationParams.OriginatingEventID;

    // Fire wrapper boundary completions BEFORE chaining downstream. Wrapper Path facts must exist before any
    // downstream prereq evaluation runs. Routes through the shared FireWrapperBoundaryCompletion helper so
    // the wrapper's full outcome chain fires (including loop-back wires) — symmetric with ChainToNextNodes's
    // FireBoundaryCompletion lambda. Empty when the utility's forward output doesn't cross a wrapper Exit
    // (the common mid-graph utility chaining case).
    for (const FQuestBoundaryCompletion& BC : Node->GetBoundaryCompletionsOnForward())
    {
        UE_LOG(LogSimpleQuest, Verbose,
            TEXT("HandleOnNodeForwardActivated: firing boundary completion '%s' outcome='%s' (from utility '%s')"),
            *BC.WrapperTagName.ToString(),
            *BC.OutcomeTag.ToString(),
            *Node->GetContextualTag().ToString());

        FireWrapperBoundaryCompletion(BC, InheritedEventID);
    }

    // Thread the source utility node's PendingActivationParams onto each downstream destination so any payload
    // that arrived at the utility (via signal-driven self-stamp on UActivationGroupListenerNode, cascade, or direct
    // upstream stamp) propagates through the forward chain. Mirrors ChainToNextNodes::StampAndActivate. Identity
    // for utility nodes that don't carry payload (SetBlocked / ClearBlocked) — those fields stay zero-init either
    // way so the stamp is a harmless overwrite. OriginatingEventID rides through this wholesale copy.
    for (const FName& Tag : Node->GetNextNodesOnForward())
    {
        if (UQuestNodeBase* DestInstance = LoadedNodeInstances.FindRef(Tag))
        {
            DestInstance->PendingActivationParams = Node->PendingActivationParams;
        }
        ActivateNodeByTag(Tag, EQuestActivationProvenance::ChainCascade);
    }
}

void UQuestManagerSubsystem::ActivateNodeByTag(FName NodeTagName, EQuestActivationProvenance Provenance, FGameplayTag IncomingOutcomeTag, FName IncomingSourceTag, bool bBypassGiverGate)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(UQuestManagerSubsystem_ActivateNodeByTag);

    TObjectPtr<UQuestNodeBase>* InstancePtr = LoadedNodeInstances.Find(NodeTagName);
    if (!InstancePtr || !*InstancePtr)
    {
        UE_LOG(LogSimpleQuest, Warning, TEXT("UQuestManagerSubsystem::ActivateNodeByTag : no instance found for tag name '%s'"), *NodeTagName.ToString());
        return;
    }

    UQuestNodeBase* Instance = *InstancePtr;

    // Stamp activation provenance on the destination's PendingActivationParams. ActivateInternal merges this into
    // ReceivedActivationParams, and HandleOnNodeStarted's Step-side RecordEntry reads the snapshot's Provenance into
    // FQuestEntryArrival. Stamped after lookup, before the rest of ActivateNodeByTag's flow touches PendingActivation-
    // Params, so this value rides through the merge regardless of whether the caller pre-stamped other fields on the struct.
    Instance->PendingActivationParams.Provenance = Provenance;

    const FGameplayTag NodeTag = UGameplayTagsManager::Get().RequestGameplayTag(NodeTagName, false);

    // Route the diamond + giver-gate + Block-gate decision through the centralized policy evaluator. ActivateNodeByTag
    // becomes a switch over the decision; existing handlers (logging, SetQuestPendingGiver + event publishes + watch
    // registration, the Deactivated clear, the cascade-origin / IncomingOutcomeTag / PendingEntryActivations side effects,
    // the Activate call) stay verbatim, just relocated under their decision case. See FQuestActivationGuard for the
    // policy logic and EQuestActivationGuardDecision for the case enumeration.

    // Multi-tag aware giver-presence check. A giver actor authored against a step's standalone-perspective tag (the
    // natural authoring form) registers under that tag in RegisteredGiverQuestTags. When the same step is reached via
    // a LinkedQuestline placement, its NodeTag is the contextualized form (parent-prefixed) and its AssetScopedAlias-
    // Tags carry the standalone form. Walk both so the giver gate fires on inlined steps the same way it fires on
    // top-level ones — without this, every inlined step bypasses its giver gate and activates immediately.
    bool bHasRegisteredGiver = NodeTag.IsValid() && RegisteredGiverQuestTags.Contains(NodeTag);
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

    // Step diamond refusal — early out BEFORE the Deactivated clear (matches original behavior where Step refusal
    // returned before reaching the clear). Steps own their state directly; re-firing would corrupt lifecycle invariants
    // and double-publish FQuestStartedEvent.
    if (Decision == EQuestActivationGuardDecision::RefuseStepAlreadyLive)
    {
        UE_LOG(LogSimpleQuest, Verbose, TEXT("ActivateNodeByTag: '%s' skipped (already live)"),
            *NodeTagName.ToString());
        return;
    }
    if (Decision == EQuestActivationGuardDecision::RefuseStepAlreadyPendingGiver)
    {
        UE_LOG(LogSimpleQuest, Verbose, TEXT("ActivateNodeByTag: '%s' skipped (already pending giver)"),
            *NodeTagName.ToString());
        return;
    }

    // Clear Deactivated for any non-Step-refusal path. Matches the original behavior where ANY Activate-input pulse
    // clears Deactivated, even if downstream guards (Block) ultimately refuse the activation. A deactivated node is
    // allowed to re-enter via its Activate input.
    if (NodeTag.IsValid() && WorldState)
    {
        WorldState->RemoveFact(FQuestTagComposer::ResolveStateFactTag(NodeTag, EQuestStateLeaf::Deactivated));
    }

    switch (Decision)
    {
    case EQuestActivationGuardDecision::RefuseBlocked:
        // Block gate — orthogonal to lifecycle. Block intentionally allows the giver-gate fire (so the giver stays
        // visible and the player can attempt to interact) but refuses the actual SetQuestLive transition. This mirrors
        // HandleGiveQuestEvent's blocker check at the give step: gives on Blocked quests are refused with FQuestGive-
        // BlockedEvent, and direct/cascade activations on Blocked quests are refused here. Together these make Block a
        // pure re-initiation gate that doesn't disable targets/givers (those are SetQuestDeactivated's job).
        UE_LOG(LogSimpleQuest, Verbose, TEXT("ActivateNodeByTag: '%s' skipped — Blocked (re-initiation refused, giver/targets untouched)"),
            *NodeTagName.ToString());
        return;

    case EQuestActivationGuardDecision::GiverGateFire:
        // Giver gate — sets PendingGiver state, publishes FQuestActivatedEvent (always) plus FQuestEnabledEvent if
        // prereqs are already satisfied, and registers an EnablementWatch when prereqs are non-Always so the gate can
        // re-publish Enabled when leaves satisfy mid-PendingGiver. Block is intentionally NOT pre-checked — Block-on-
        // giver-gated quests still publishes the activation events so the giver stays visible/interactive.
        Instance->bWasGiverGated = true;
        SetQuestPendingGiver(NodeTag);

        if (QuestSignalSubsystem)
        {
            FQuestEventContext Context = AssembleEventContext(Instance, FQuestObjectiveContext());
            const FQuestPrereqStatus PrereqStatus = Instance->PrerequisiteExpression.EvaluateWithLeafStatus(WorldState, QuestStateSubsystem);

            // Push the prereq status to the state subsystem before publishing events. The state subsystem's
            // QueryQuestActivationBlockers reads this cache; subsequent designer queries (or our own
            // HandleGiveQuestEvent acceptance gate) will see the correct PrereqUnmet status.
            if (UQuestStateSubsystem* StateSubsystem = GetGameInstance() ? GetGameInstance()->GetSubsystem<UQuestStateSubsystem>() : nullptr)
            {
                StateSubsystem->UpdateQuestPrereqStatus(NodeTag, PrereqStatus);
            }

            FQuestPublish::OnAllNodeTags(QuestSignalSubsystem, Instance, FQuestActivatedEvent(NodeTag, Context, PrereqStatus));

            if (PrereqStatus.bSatisfied)
            {
                FQuestPublish::OnAllNodeTags(QuestSignalSubsystem, Instance, FQuestEnabledEvent(NodeTag, Context));
            }

            if (!Instance->PrerequisiteExpression.IsAlways())
            {
                RegisterEnablementWatch(NodeTag, NodeTagName, Instance->PrerequisiteExpression, PrereqStatus.bSatisfied);
            }

            UE_LOG(LogSimpleQuest, Log, TEXT("ActivateNodeByTag: '%s' gated by giver — Activated published, prereqs %s"),
                *NodeTagName.ToString(),
                PrereqStatus.bSatisfied ? TEXT("satisfied (Enabled fired)") : TEXT("unmet (watching for satisfy)"));
        }
        return;

    case EQuestActivationGuardDecision::ContainerReentry:
        // Container reentry — containers don't directly write the Live fact; their Live state derives from inner Step
        // state. Re-activating while already Live falls through to the full activation flow so HandleOnNodeStarted's
        // container branch processes Any-Outcome and per-path entries, records entry to UQuestStateSubsystem, and re-
        // publishes FQuestStartedEvent. Loop-back wires (own outer outcome → own Activate) and external fan-in both
        // route through here equivalently. Inner Step diamond guards handle idempotency for already-Live Steps.
        UE_LOG(LogSimpleQuest, Verbose, TEXT("ActivateNodeByTag: '%s' re-activating container while %s"),
            *NodeTagName.ToString(),
            FQuestLifecycleQuery::IsLive(WorldState, NodeTag) ? TEXT("live") : TEXT("pending giver"));
        break;

    case EQuestActivationGuardDecision::GiverGateSkipPathAware:
        // Path-aware giver-gate skip — for containers, all Steps reachable from the entered Activate pin (compile-time
        // populated by ComputeContainerReachability into UQuest::ReachableStepsByActivatePin) are already Live. There's
        // no work for the giver to enable, so skip the gate and fall through to normal activation. Covers loop-back
        // wires, fan-in re-entry, and any case where the entered path's targets are already running — preventing the
        // giver from spuriously re-firing each iteration.
        UE_LOG(LogSimpleQuest, Verbose, TEXT("ActivateNodeByTag: '%s' giver-gate skipped — all reachable Steps from pin '%s' already Live"),
            *NodeTagName.ToString(),
            IncomingOutcomeTag.IsValid() ? *IncomingOutcomeTag.GetTagName().ToString() : TEXT("AnyOutcome"));
        break;

    case EQuestActivationGuardDecision::Proceed:
        // No diamond hit, no giver gate, not blocked — normal forward activation.
        break;

    case EQuestActivationGuardDecision::RefuseStepAlreadyLive:
    case EQuestActivationGuardDecision::RefuseStepAlreadyPendingGiver:
        // Unreachable — handled by the early-return block above. Listed explicitly so future enum additions force a
        // compiler -Wswitch warning if someone forgets to handle a new decision case.
        return;
    }

    // Cascade origin fallback: ChainToNextNodes pre-stamps OriginTag and OriginChain for every cascade destination, so
    // this block is effectively a no-op on the normal cascade path. Retained as a safety net for any direct caller that
    // passes IncomingSourceTag without pre-stamping. Guard on empty OriginChain so the chain propagation built by
    // ChainToNextNodes isn't stomped with a double-append.
    if (IncomingSourceTag != NAME_None)
    {
        if (Instance->PendingActivationParams.OriginChain.Num() == 0)
        {
            const FGameplayTag SourceTag = UGameplayTagsManager::Get().RequestGameplayTag(IncomingSourceTag, false);
            if (SourceTag.IsValid())
            {
                Instance->PendingActivationParams.OriginTag = SourceTag;
                Instance->PendingActivationParams.OriginChain.Add(SourceTag);
            }
        }
    }

    // Stash the incoming outcome on the instance so HandleOnNodeStarted's UQuest branch can route inner entries
    // post-prereq-gate. UQuest's inner-entry activation used to run inline below this Activate call (pre-fix),
    // bypassing UQuestNodeBase::Activate's deferred-prereq path. Routing it through HandleOnNodeStarted ensures
    // inner entries activate only after ActivateInternal actually fires - synchronously when prereqs are satisfied
    // immediately, or later via TryActivateDeferred when a leaf fact arrives.
    Instance->PendingActivationParams.IncomingOutcomeTag = IncomingOutcomeTag;

    // For UQuest containers, snapshot this cascade's params into the per-cascade queue. HandleOnNodeStarted drains the
    // queue and fires entry routes for each cascade - necessary so fan-in convergence patterns (multiple upstream
    // outcomes converging at a single deferred Quest) all route correctly when the prereq satisfies. Without this
    // snapshot, only the most-recently-stamped IncomingOutcomeTag would survive, dropping earlier cascades.
    if (UQuest* QuestNode = Cast<UQuest>(Instance))
    {
        QuestNode->PendingEntryActivations.Add(QuestNode->PendingActivationParams);
    }

    Instance->Activate(NodeTag);

    UE_LOG(LogSimpleQuest, Log, TEXT("ActivateNodeByTag: '%s' activated (source '%s', outcome '%s')"),
        *NodeTagName.ToString(),
        *IncomingSourceTag.ToString(),
        *IncomingOutcomeTag.ToString());
}

void UQuestManagerSubsystem::ChainToNextNodes(UQuestNodeBase* Node, FGameplayTag OutcomeTag, FName PathIdentity, const FOriginatingEventID& OriginatingEventID)
{
    if (!Node) return;

    TRACE_CPUPROFILER_EVENT_SCOPE(UQuestManagerSubsystem_ChainToNextNodes);

    // Cycle guard: refuses recursive re-entry on the same node tag within a single synchronous chain.
    // Required because FireBoundaryCompletion now routes wrapper resolutions back through ChainToNextNodes
    // so wrapper outcome wires actually fire (closing the gap that broke outer-Quest-loops-on-itself topologies).
    // Without this guard, a degenerate authoring path — e.g., ActivationGroup wired entry→exit with no gating
    // step between, then the exit loops the parent wrapper — would stack-overflow because every iteration
    // synchronously re-enters the wrapper. Legitimate nested-wrapper recursion always climbs to outer tags
    // (Step → wrapper → grandparent), never revisits an in-flight tag, so this guard has no false positives.
    const FName NodeTagName = Node->GetContextualTag().GetTagName();
    if (ChainRecursionTags.Contains(NodeTagName))
    {
        UE_LOG(LogSimpleQuest, Error,
            TEXT("ChainToNextNodes: synchronous cycle detected on '%s' outcome='%s' — aborting recursive re-entry. ")
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
    UE_LOG(LogSimpleQuest, Log, TEXT("ChainToNextNodes: '%s' outcome='%s' path='%s' - %d path + %d any-outcome downstream node(s)"),
        *Node->GetContextualTag().ToString(),
        *OutcomeTag.ToString(),
        *ResolvedPath.ToString(),
        PathCount,
        Node->GetNextNodesOnAnyOutcome().Num());

    if (Node->GetContextualTag().IsValid())
    {
        SetQuestResolved(Node->GetContextualTag(), OutcomeTag, EQuestResolutionSource::Graph);
        if (QuestSignalSubsystem)
        {
            if (FDelegateHandle* Handle = LiveStepTriggerHandles.Find(Node->GetContextualTag()))
            {
                QuestSignalSubsystem->UnsubscribeMessage(Node->GetContextualTag(), *Handle);
                LiveStepTriggerHandles.Remove(Node->GetContextualTag());
            }
        }
    }

    PublishQuestEndedEvent(Node, OutcomeTag, EQuestResolutionSource::Graph);

    /**
     * Thread this node's compiled ContextualTag (as FName) forward as IncomingSourceTag so any Quest destination in the next layer
     * can filter its source-qualified entries against the originator of this outcome.
     */
    const FName SourceTagName = Node->GetContextualTag().GetTagName();

    // Gather forward params from the completing step (designer-supplied via CompleteObjectiveWithOutcome)
    // and build the OriginChain extension (received chain + this step's tag) so downstream steps see the full history.
    FQuestObjectiveActivationParams ForwardPayload;
    TArray<FGameplayTag> ForwardChain;
    if (const UQuestStep* CompletingStep = Cast<UQuestStep>(Node))
    {
        ForwardPayload = CompletingStep->GetCompletionForwardParams();
        ForwardChain = CompletingStep->GetReceivedActivationParams().OriginChain;
    }
    if (Node->GetContextualTag().IsValid())
    {
        ForwardChain.Add(Node->GetContextualTag());
    }

    auto StampAndActivate = [this, &ForwardPayload, &ForwardChain, OutcomeTag, SourceTagName, &Node, &OriginatingEventID](const FName& DestTagName)
    {
        if (UQuestNodeBase* DestInstance = LoadedNodeInstances.FindRef(DestTagName))
        {
            DestInstance->PendingActivationParams = ForwardPayload;
            DestInstance->PendingActivationParams.OriginTag = Node->GetContextualTag();
            DestInstance->PendingActivationParams.OriginChain = ForwardChain;
            DestInstance->PendingActivationParams.OriginatingEventID = OriginatingEventID;
        }
        ActivateNodeByTag(DestTagName, EQuestActivationProvenance::ChainCascade, OutcomeTag, SourceTagName);
    };

    // Named-outcome path: when this path terminates at the OUTERMOST root scope (no wrapper to cascade into),
    // the inner-most asset-level resolution publishes first (cascade-direction event-order invariant: outward
    // flow → inner publishes first). When a wrapper IS available, skip the explicit graph-resolution publish
    // here — the wrapper's resolution downstream publishes on its ContextualTag + AssetScopedAliasTags via
    // PublishWithAliases, and the wrapper's alias array includes the inner asset's identity tag. So the
    // wrapper's resolution already publishes on the inner asset's identity. Firing here too would double-publish.
    // After resolution: boundary completions fire so wrapper Path facts exist before destination prereq
    // evaluation runs; direct downstream destinations activate last.
    if (const FQuestPathNodeList* PathList = Node->GetNextNodesByPath().Find(ResolvedPath))
    {
        if (PathList->BoundaryCompletions.IsEmpty())
        {
            PublishGraphResolutions(PathList->ExitedGraphTags, OutcomeTag, EQuestResolutionSource::Graph);
        }
        for (const FQuestBoundaryCompletion& BC : PathList->BoundaryCompletions)
        {
            FireWrapperBoundaryCompletion(BC, OriginatingEventID);
        }
        for (const FName& Tag : PathList->NodeTags)
        {
            StampAndActivate(Tag);
        }
    }

    // Any-outcome path: same ordering and same dedup rule. Wrapper alias-publish handles the inner-asset case.
    if (Node->GetBoundaryCompletionsOnAnyOutcome().IsEmpty())
    {
        PublishGraphResolutions(Node->GetExitedGraphTagsOnAnyOutcome(), OutcomeTag, EQuestResolutionSource::Graph);
    }
    for (const FQuestBoundaryCompletion& BC : Node->GetBoundaryCompletionsOnAnyOutcome())
    {
        FireWrapperBoundaryCompletion(BC, OriginatingEventID);
    }
    for (const FName& Tag : Node->GetNextNodesOnAnyOutcome())
    {
        StampAndActivate(Tag);
    }
}

void UQuestManagerSubsystem::SetQuestDeactivated(FGameplayTag QuestTag, EDeactivationSource Source)
{
    if (!QuestTag.IsValid() || !WorldState) return;

    // Inclusive precondition: deactivation only makes sense if there's an active lifecycle to interrupt: Live or
    // PendingGiver. The prior exclusive "skip if Completed" guard mishandled loopable quests (Any Outcome routing
    // back to own Activate), where Completed remains asserted across loop iterations alongside a freshly-set
    // Live fact. Asking "is there an active lifecycle?" answers the actual question; "is it not yet completed?"
    // produces false negatives on multi-resolution quests.
    if (!FQuestLifecycleQuery::HasActiveLifecycle(WorldState, QuestTag))
    {
        UE_LOG(LogSimpleQuest, Verbose, TEXT("SetQuestDeactivated: '%s' skipped - no Live or PendingGiver lifecycle to interrupt"), *QuestTag.ToString());
        return;
    }

    // Snapshot Deactivated state before doing the cleanup work. Live/PendingGiver + Deactivated co-occurring is an
    // inconsistent state — normal flow has ActivateNodeByTag clearing Deactivated before SetQuestLive runs, so the
    // two should never be both asserted. If they are (manual fact write, save/load round-trip, race in a callback),
    // we still need to clear Live / PendingGiver — that's the actual point of deactivation. We just skip
    // re-asserting Deactivated (would bump the WorldState ref-count, leaving Deactivated pinned past a future
    // ClearBlocked / similar) and the FQuestDeactivatedEvent re-publish (subscribers already saw the original
    // transition).
    const bool bWasAlreadyDeactivated = FQuestLifecycleQuery::IsDeactivated(WorldState, QuestTag);
    if (bWasAlreadyDeactivated)
    {
        UE_LOG(LogSimpleQuest, Warning,
            TEXT("SetQuestDeactivated: '%s' had Live or PendingGiver AND Deactivated asserted simultaneously — inconsistent state; clearing active facts, skipping Deactivated fact-write + event re-publish"),
            *QuestTag.ToString());
    }
    
    RecentGiverActors.Remove(QuestTag);
    
	const FName TagName = QuestTag.GetTagName();

	UE_LOG(LogSimpleQuest, Log, TEXT("SetQuestDeactivated: '%s' source=%s"),
		*QuestTag.ToString(),
		Source == EDeactivationSource::External ? TEXT("External") : TEXT("Internal"));

	// Look up node instance early. Needed for DeactivateInternal and context assembly.
	UQuestNodeBase* Node = nullptr;
	if (TObjectPtr<UQuestNodeBase>* NodePtr = LoadedNodeInstances.Find(TagName))
	{
		Node = *NodePtr;
	}

    // PendingGiver cleanup. Don't mutate RegisteredGiverQuestTags — it's the structural "this quest has a giver"
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
            WorldState->RemoveFact(FQuestTagComposer::ResolveStateFactTag(QuestTag, EQuestStateLeaf::Live));
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
        // re-derives its Live state. Containers skip — they don't own a Live fact directly to walk away from.
        if (Node && Node->IsStepNode())
        {
            for (const FGameplayTag& AncestorTag : Cast<UQuestStep>(Node)->GetAncestorContainerTags())
            {
                DeriveContainerLive(AncestorTag);
            }
        }
    }

    // Skip the Deactivated fact write + event publish if Deactivated was already asserted (the ref-count bump
    // would prevent a future ClearBlocked from fully clearing Deactivated; the event re-publish would deliver a
    // duplicate signal). Live / PendingGiver cleanup above ran unconditionally — that's the actual point of this
    // function.
    if (!bWasAlreadyDeactivated)
    {
        WorldState->AddFact(FQuestTagComposer::ResolveStateFactTag(QuestTag, EQuestStateLeaf::Deactivated));

        // Publish with context
        if (QuestSignalSubsystem)
        {
            FQuestDeactivatedEvent Event(QuestTag, Source);
            if (Node)
            {
                Event = FQuestDeactivatedEvent(QuestTag, Source, AssembleEventContext(Node, FQuestObjectiveContext()));
                FQuestPublish::OnAllNodeTags(QuestSignalSubsystem, Node, Event);
            }
            else
            {
                // Fallback — no instance loaded under this tag. Single publish preserves observability.
                QuestSignalSubsystem->PublishMessage(QuestTag, Event);
            }
        }
    }
}

void UQuestManagerSubsystem::HandleNodeDeactivatedEvent(FGameplayTag Channel, const FQuestDeactivatedEvent& Event)
{
    const FName TagName = Channel.GetTagName();
    TObjectPtr<UQuestNodeBase>* NodePtr = LoadedNodeInstances.Find(TagName);
    if (!NodePtr) return;

    UQuestNodeBase* Node = *NodePtr;

    UE_LOG(LogSimpleQuest, Log, TEXT("HandleNodeDeactivatedEvent: '%s' — activating %d, cascading deactivation to %d"),
        *Channel.ToString(),
        Node->GetNextNodesOnDeactivation().Num(),
        Node->GetNextNodesToDeactivateOnDeactivation().Num());
    
    // Activate downstream nodes wired from the Deactivated output to their Activate input.
    for (const FName& Tag : Node->GetNextNodesOnDeactivation())
    {
        ActivateNodeByTag(Tag, EQuestActivationProvenance::ChainCascade);
    }
    
    // Cascade deactivation to nodes wired from the Deactivated output to a Deactivate input.
    for (const FName& Tag : Node->GetNextNodesToDeactivateOnDeactivation())
    {
        const FGameplayTag TargetTag = UGameplayTagsManager::Get().RequestGameplayTag(Tag, false);
        if (TargetTag.IsValid()) SetQuestDeactivated(TargetTag, Event.Source);
    }
}

void UQuestManagerSubsystem::PublishQuestEndedEvent(const UQuestNodeBase* Node, FGameplayTag OutcomeTag, EQuestResolutionSource Source) const
{
    if (!QuestSignalSubsystem || !Node->GetContextualTag().IsValid()) return;

    FQuestObjectiveContext CompletionCtx;
    if (const UQuestStep* Step = Cast<UQuestStep>(Node))
    {
        CompletionCtx = Step->GetCompletionContext();
    }

    FQuestEventContext Context = AssembleEventContext(Node, CompletionCtx);
    FQuestPublish::OnAllNodeTags(QuestSignalSubsystem, Node, FQuestEndedEvent(Node->GetContextualTag(), OutcomeTag, Source, Context));
}

void UQuestManagerSubsystem::HandleGiveQuestEvent(FGameplayTag Channel, const FQuestGivenEvent& Event)
{
    const FGameplayTag QuestTag = Event.GetQuestTag();
    if (!QuestTag.IsValid()) return;

    // Blocker gate — quest-level identity check, evaluated against the input tag (the standalone-perspective form
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
        FQuestPublish::OnAllTagsForRequest(QuestSignalSubsystem, QuestTag, LoadedNodeInstances, FQuestGiveBlockedEvent(QuestTag, Blockers, Event.Params.ActivationSource));

        // Build the debug warning message, showing each blocker in a bulleted list, with sub-bullets for specific unmet prereq leaves
        FString Message = FString::Printf(TEXT("HandleGiveQuestEvent: '%s' refused — %d blocker%s:"),
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
        UE_LOG(LogSimpleQuest, Warning, TEXT("%s"), *Message);
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

    UE_LOG(LogSimpleQuest, Log, TEXT("HandleGiveQuestEvent: '%s' — clearing PendingGiver, activating %d placement(s) (CustomData %s, ActivationSource %s)"),
        *QuestTag.ToString(),
        CanonicalTags.Num(),
        Event.Params.CustomData.IsValid() ? TEXT("populated") : TEXT("empty"),
        Event.Params.ActivationSource ? *Event.Params.ActivationSource->GetName() : TEXT("null"));

    for (const FGameplayTag& CanonicalTag : CanonicalTags)
    {
        if (!CanonicalTag.IsValid()) continue;

        UQuestNodeBase* Instance = LoadedNodeInstances.FindRef(CanonicalTag.GetTagName());
        if (!Instance) continue;  // Skip canonicals with no loaded instance — avoids ActivateNodeByTag's noisy warning.

        if (Event.Params.ActivationSource)
        {
            // Stored under each placement's ContextualTag so HandleOnNodeStarted's per-instance lookup
            // (RecentGiverActors.Find(Node->GetContextualTag())) recovers the giver actor for FQuestStartedEvent
            // attribution on every placement that activates from this give.
            RecentGiverActors.Add(CanonicalTag, Event.Params.ActivationSource);
        }

        ClearQuestPendingGiver(CanonicalTag);
        ClearEnablementWatch(CanonicalTag);

        // Mirror of HandleActivationRequest: stash the giver-authored params on the target step so ActivateInternal
        // merges them with the step's authored defaults. Empty Params stamps cleanly. Additive merge preserves the
        // step's defaults in that case.
        if (UQuestStep* Step = Cast<UQuestStep>(Instance))
        {
            Step->PendingActivationParams = Event.Params;
        }

        ActivateNodeByTag(CanonicalTag.GetTagName(), EQuestActivationProvenance::GiverGate, FGameplayTag(), NAME_None, true);
    }
}

void UQuestManagerSubsystem::HandleActivationRequest(FGameplayTag Channel, const FQuestActivationRequestEvent& Event)
{
    const FGameplayTag QuestTag = Event.GetQuestTag();
    if (!QuestTag.IsValid()) return;

    // Resolve canonical tags so a request authored against the standalone-perspective form (the natural BP-side
    // authoring tag) reaches every active placement — standalone + every aliased contextual. Without this, an
    // external RequestActivation against an alias-form tag only targets the standalone placement and leaves
    // inlined contextual placements stranded. Single-instance / non-aliased case collapses to one iteration.
    UQuestStateSubsystem* StateSubsystem = GetGameInstance() ? GetGameInstance()->GetSubsystem<UQuestStateSubsystem>() : nullptr;
    TArray<FGameplayTag> CanonicalTags = StateSubsystem ? StateSubsystem->ResolveCanonicalTags(QuestTag) : TArray<FGameplayTag>{ QuestTag };

    UE_LOG(LogSimpleQuest, Log, TEXT("HandleActivationRequest: '%s' — activating %d placement(s) with external params (CustomData %s)"),
        *QuestTag.ToString(),
        CanonicalTags.Num(),
        Event.Params.CustomData.IsValid() ? TEXT("populated") : TEXT("empty"));

    for (const FGameplayTag& CanonicalTag : CanonicalTags)
    {
        if (!CanonicalTag.IsValid()) continue;

        UQuestNodeBase* Instance = LoadedNodeInstances.FindRef(CanonicalTag.GetTagName());
        if (!Instance) continue;  // Skip canonicals with no loaded instance — avoids ActivateNodeByTag's noisy warning.

        // Stash params on the target step (if it IS a step) before activation, so ActivateInternal merges them
        // with the Step's authored defaults.
        if (UQuestStep* Step = Cast<UQuestStep>(Instance))
        {
            Step->PendingActivationParams = Event.Params;
        }

        ActivateNodeByTag(CanonicalTag.GetTagName(), EQuestActivationProvenance::ExternalAPI);
    }
}

void UQuestManagerSubsystem::HandleBlockRequest(FGameplayTag Channel, const FQuestBlockRequestEvent& Event)
{
    const FGameplayTag QuestTag = Event.GetQuestTag();
    if (!QuestTag.IsValid() || !WorldState) return;

    const FGameplayTag BlockedFact = FQuestTagComposer::ResolveStateFactTag(QuestTag, EQuestStateLeaf::Blocked);
    if (!BlockedFact.IsValid()) return;

    // Idempotency guard: symmetric with the already-deactivated guard in SetQuestDeactivated. Spamming a block
    // request on an already-blocked quest would otherwise bump the WorldState ref-count without reflecting a
    // genuine state transition. Also gates FQuestBlockedEvent broadcast so already-blocked re-applications stay
    // silent at the event layer.
    if (FQuestLifecycleQuery::IsBlocked(WorldState, QuestTag))
    {
        UE_LOG(LogSimpleQuest, Verbose, TEXT("HandleBlockRequest: '%s' skipped — already blocked"), *QuestTag.ToString());
        return;
    }

    WorldState->AddFact(BlockedFact);

    // Block does NOT auto-deactivate. Block is the orthogonal re-entry gate; deactivation is the lifecycle/world
    // disabler. Callers who want both must publish FQuestDeactivateRequestEvent themselves (or use SetBlocked's
    // bAlsoDeactivateTargets toggle on the node-driven path).
    FQuestPublish::OnAllTagsForRequest(QuestSignalSubsystem, QuestTag, LoadedNodeInstances, FQuestBlockedEvent(QuestTag, Event.Source));

    UE_LOG(LogSimpleQuest, Log, TEXT("HandleBlockRequest: '%s' — Blocked fact added, FQuestBlockedEvent published (source=%s)"),
        *QuestTag.ToString(),
        Event.Source == EDeactivationSource::External ? TEXT("External") : TEXT("Internal"));
}

void UQuestManagerSubsystem::HandleClearBlockRequest(FGameplayTag Channel, const FQuestClearBlockRequestEvent& Event)
{
    const FGameplayTag QuestTag = Event.GetQuestTag();
    if (!QuestTag.IsValid() || !WorldState) return;

    const FGameplayTag BlockedFact = FQuestTagComposer::ResolveStateFactTag(QuestTag, EQuestStateLeaf::Blocked);
    if (!BlockedFact.IsValid()) return;

    // Symmetric with the already-blocked guard in HandleBlockRequest. Also gates FQuestUnblockedEvent broadcast
    // so clear-on-already-unblocked stays silent at the event layer.
    if (!FQuestLifecycleQuery::IsBlocked(WorldState, QuestTag))
    {
        UE_LOG(LogSimpleQuest, Verbose, TEXT("HandleClearBlockRequest: '%s' skipped — not currently blocked"), *QuestTag.ToString());
        return;
    }

    WorldState->ClearFact(BlockedFact);
    // Deactivated intentionally not cleared: the target's Activate input clears it on re-entry.

    if (QuestSignalSubsystem)
    {
        QuestSignalSubsystem->PublishMessage(QuestTag, FQuestUnblockedEvent(QuestTag, Event.Source));
    }

    UE_LOG(LogSimpleQuest, Log, TEXT("HandleClearBlockRequest: '%s' — Blocked fact cleared, FQuestUnblockedEvent published (source=%s)"),
        *QuestTag.ToString(),
        Event.Source == EDeactivationSource::External ? TEXT("External") : TEXT("Internal"));
}

void UQuestManagerSubsystem::HandleResolveRequest(FGameplayTag Channel, const FQuestResolveRequestEvent& Event)
{
    const FGameplayTag QuestTag = Event.GetQuestTag();
    if (!QuestTag.IsValid() || !WorldState) return;

    // Override guard — skip if already in a terminal state unless designer explicitly opts in. Default-false
    // protects against accidental double-broadcast; opt-in true appends additively (never removes prior facts).
    if (!Event.bOverrideExisting && FQuestLifecycleQuery::IsTerminal(WorldState, QuestTag))
    {
        UE_LOG(LogSimpleQuest, Warning,
            TEXT("HandleResolveRequest: '%s' skipped — already in terminal state. Pass bOverrideExisting=true to append a new resolution entry additively."),
            *QuestTag.ToString());
        return;
    }


    SetQuestResolved(QuestTag, Event.OutcomeTag, EQuestResolutionSource::External);

    // Live-step bookkeeping cleanup mirroring ChainToNextNodes — defensive against the non-Live cases (Find returns null).
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

    // Publish FQuestEndedEvent — branch on whether a node instance is loaded for context assembly.
    if (QuestSignalSubsystem)
    {
        if (UQuestNodeBase* Node = LoadedNodeInstances.FindRef(QuestTag.GetTagName()))
        {
            PublishQuestEndedEvent(Node, Event.OutcomeTag, EQuestResolutionSource::External);
        }
        else
        {
            // Fully-dynamic flow — no node instance. Publish a minimal event without assembled Context.
            QuestSignalSubsystem->PublishMessage(QuestTag, FQuestEndedEvent(QuestTag, Event.OutcomeTag, EQuestResolutionSource::External));
        }
    }

    UE_LOG(LogSimpleQuest, Log, TEXT("HandleResolveRequest: '%s' resolved with outcome='%s' (override=%d)"),
        *QuestTag.ToString(), *Event.OutcomeTag.ToString(), Event.bOverrideExisting ? 1 : 0);
}

void UQuestManagerSubsystem::HandleQuestlineStartRequest(FGameplayTag Channel, const FQuestlineStartRequestEvent& Event)
{
    if (Event.Graph.IsNull())
    {
        UE_LOG(LogSimpleQuest, Warning, TEXT("HandleQuestlineStartRequest: null graph reference, skipping"));
        return;
    }

    // Hot path: already-loaded graph activates immediately.
    if (UQuestlineGraph* AlreadyLoaded = Event.Graph.Get())
    {
        UE_LOG(LogSimpleQuest, Log, TEXT("HandleQuestlineStartRequest: '%s' already loaded, activating immediately"), *AlreadyLoaded->GetName());
        ActivateQuestlineGraph(AlreadyLoaded);
        return;
    }

    // Cold path: async load via FStreamableManager, activate on completion. CreateWeakLambda binds the lambda
    // to this UObject's weak pointer so a manager Deinitialize mid-load makes the callback a no-op rather than
    // a crash. Pattern prototype for 0.5.0's runtime asset loading pass.
    UE_LOG(LogSimpleQuest, Log, TEXT("HandleQuestlineStartRequest: '%s' cold, async-loading"), *Event.Graph.ToString());

    FStreamableManager& StreamableManager = UAssetManager::GetStreamableManager();
    const TSoftObjectPtr<UQuestlineGraph> SoftGraph = Event.Graph;
    StreamableManager.RequestAsyncLoad(
        SoftGraph.ToSoftObjectPath(),
        FStreamableDelegate::CreateWeakLambda(this, [this, SoftGraph]()
        {
            if (UQuestlineGraph* Graph = SoftGraph.Get())
            {
                UE_LOG(LogSimpleQuest, Log, TEXT("HandleQuestlineStartRequest: async-load complete for '%s', activating"), *Graph->GetName());
                ActivateQuestlineGraph(Graph);
            }
            else
            {
                UE_LOG(LogSimpleQuest, Warning, TEXT("HandleQuestlineStartRequest: async-load completed but graph still null"));
            }
        })
    );
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
            UE_LOG(LogSimpleQuest, Verbose,
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
                UE_LOG(LogSimpleQuest, Warning,
                    TEXT("UQuestManagerSubsystem::RegisterGiversFromAssetRegistry : tag '%s' is not registered — has the questline been compiled?"),
                    *TagStr);
                continue;
            }
            RegisteredGiverQuestTags.Add(ContextualTag);
            UE_LOG(LogSimpleQuest, Verbose,
                TEXT("UQuestManagerSubsystem::RegisterGiversFromAssetRegistry : registered giver for '%s' from '%s' (asset registry)"),
                *ContextualTag.ToString(), *Asset.AssetName.ToString());
        }
    }
#endif
}

void UQuestManagerSubsystem::AutoLoadListenerBearingGraphs()
{
    IAssetRegistry& AR = FAssetRegistryModule::GetRegistry();

    FARFilter Filter;
    Filter.ClassPaths.Add(UQuestlineGraph::StaticClass()->GetClassPathName());
    Filter.bRecursiveClasses = true;

    TArray<FAssetData> Assets;
    AR.GetAssets(Filter, Assets);

    int32 PreRegisteredCount = 0;
    for (const FAssetData& Asset : Assets)
    {
        // The compiler stamps HasActivationGroupListener=true on assets whose CompiledNodes contains a
        // UActivationGroupListenerNode instance (locally authored or inlined via LinkedQuestline). Assets that
        // never compiled this flag (pre-Step-A content not yet recompiled) read missing/false and get skipped.
        FString TagValue;
        if (!Asset.GetTagValue(TEXT("HasActivationGroupListener"), TagValue)) continue;
        if (TagValue != TEXT("true")) continue;

        UQuestlineGraph* Graph = Cast<UQuestlineGraph>(Asset.GetAsset());
        if (!Graph)
        {
            UE_LOG(LogSimpleQuest, Warning,
                TEXT("AutoLoadListenerBearingGraphs: failed to load '%s' — skipping"),
                *Asset.GetObjectPathString());
            continue;
        }

        RegisterQuestlineGraph(Graph);
        ++PreRegisteredCount;

        UE_LOG(LogSimpleQuest, Log,
            TEXT("AutoLoadListenerBearingGraphs: pre-registered '%s' for cross-graph listener subscription"),
            *Graph->GetName());
    }

    UE_LOG(LogSimpleQuest, Verbose,
        TEXT("AutoLoadListenerBearingGraphs: scanned %d UQuestlineGraph asset(s); pre-registered %d listener-bearing graph(s)"),
        Assets.Num(),
        PreRegisteredCount);
}

void UQuestManagerSubsystem::CheckClassObjectives(FGameplayTag Channel, const FInstancedStruct& RawEvent)
{
    const FQuestObjectiveTriggered* Event = RawEvent.GetPtr<FQuestObjectiveTriggered>();
    if (!Event || !Event->TriggeredActor || !QuestSignalSubsystem) return;

    for (const auto& Pair : ClassFilteredSteps)
    {
        if (Event->TriggeredActor->IsA(Pair.Value))
        {
            UE_LOG(LogSimpleQuest, Verbose, TEXT("CheckClassObjectives: actor '%s' (%s) matches class filter — forwarding to step '%s'"),
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

    UE_LOG(LogSimpleQuest, Log, TEXT("DeferChainToNextNodes: '%s' outcome='%s' path='%s' — subscribed to %d prereq channel(s)"),
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

    UE_LOG(LogSimpleQuest, Log, TEXT("TryFireDeferredCompletion: '%s' — prereqs satisfied, resuming chain"), *StepTag.ToString());

    // Clean up subscriptions
    if (TMap<FGameplayTag, FPrereqLeafSubscription::FPrereqLeafHandles>* Handles = DeferredCompletionPrereqHandles.Find(StepTag))
    {
        FPrereqLeafSubscription::UnsubscribeAll(QuestSignalSubsystem, *Handles);
        DeferredCompletionPrereqHandles.Remove(StepTag);
    }

    FQuestDeferredCompletion Pending;
    DeferredCompletions.RemoveAndCopyValue(StepTag, Pending);

    // Mint the cascade event ID at the deferred fire moment — that's when the gameplay event actually happens
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
    UE_LOG(LogSimpleQuest, Verbose, TEXT("UQuestManagerSubsystem::HandleGiverRegisteredEvent : giver registered for '%s'"), *QuestTag.ToString());

    if (FQuestLifecycleQuery::IsLive(WorldState, QuestTag))
    {
        UE_LOG(LogSimpleQuest, Warning,
            TEXT("UQuestManagerSubsystem::HandleGiverRegisteredEvent : giver for '%s' came online after the quest already activated — gate was missed. Save the giver Blueprint to fix this for streaming scenarios."),
            *QuestTag.ToString());
    }
}

void UQuestManagerSubsystem::HandleNodeDeactivationRequest(FGameplayTag Channel, const FQuestDeactivateRequestEvent& Event)
{
    FGameplayTag EventTag = Event.GetQuestTag();
    if (EventTag.IsValid()) SetQuestDeactivated(EventTag, Event.Source);
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

    const FGameplayTag LiveFact = FQuestTagComposer::ResolveStateFactTag(QuestTag, EQuestStateLeaf::Live);

    // Idempotency guard: symmetric with SetQuestBlocked / SetQuestDeactivated. Fan-in convergence on a single quest
    // (two upstream paths activating the same node, especially when both arrive while an inner prereq defers) calls
    // through here once per cascade and would otherwise bump the WorldState ref-count to 2. The single RemoveFact on
    // completion or deactivation then leaves a residual Live assertion stuck on. Signal propagation upstream still
    // fires per cascade, only the boolean state fact is gated here.
    if (FQuestLifecycleQuery::IsLive(WorldState, QuestTag))
    {
        UE_LOG(LogSimpleQuest, Verbose, TEXT("SetQuestLive: '%s' already live, skipping (convergence)"), *QuestTag.ToString());
        return;
    }

    UE_LOG(LogSimpleQuest, Verbose, TEXT("SetQuestLive: '%s'"), *QuestTag.ToString());
    WorldState->AddFact(LiveFact);

    // Ancestor walk for Steps. Containers' Live state is derived from inner Step state, so a Step
    // transitioning to Live propagates upward: each ancestor container re-derives its Live fact based on whether
    // any of its inner Steps is now Live. Innermost-first ordering means the immediate parent derives first;
    // outer ancestors derive against the now-up-to-date inner state. Non-Step callers (containers passed in by
    // misuse, future node kinds) skip the walk — there's no ancestor chain to traverse for them.
    if (UQuestNodeBase* Node = LoadedNodeInstances.FindRef(QuestTag.GetTagName()))
    {
        if (Node->IsStepNode())
        {
            for (const FGameplayTag& AncestorTag : Cast<UQuestStep>(Node)->GetAncestorContainerTags())
            {
                DeriveContainerLive(AncestorTag);
            }
        }
    }
}

void UQuestManagerSubsystem::DeriveContainerLive(FGameplayTag ContainerTag)
{
    if (!WorldState || !ContainerTag.IsValid()) return;

    UQuest* Container = Cast<UQuest>(LoadedNodeInstances.FindRef(ContainerTag.GetTagName()));
    if (!Container) return;  // not a container — nothing to derive

    // A container is Live whenever any inner Step at any depth has an active lifecycle (Live or PendingGiver) —
    // a giver-gated inner Step keeps its container visibly in-progress because the player can interact with the
    // giver to advance. InnerStepTags is compile-time populated by ComputeContainerReachability and bounded by
    // the number of Steps inside this wrapper (typically a handful), so the linear scan is cheap. Short-circuits
    // on the first active inner Step.
    bool bAnyInnerLive = false;
    for (const FGameplayTag& InnerStepTag : Container->GetInnerStepTags())
    {
        if (FQuestLifecycleQuery::HasActiveLifecycle(WorldState, InnerStepTag))
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
        WorldState->AddFact(ContainerLiveFact);
        UE_LOG(LogSimpleQuest, Verbose, TEXT("DeriveContainerLive: '%s' → Live (inner Step now active)"), *ContainerTag.ToString());
    }
    else if (!bAnyInnerLive && bCurrentlyLive)
    {
        WorldState->RemoveFact(ContainerLiveFact);
        // ResolvedByEvents is intentionally NOT cleared here. Under multi-tag fanout, a single logical gameplay
        // event can produce two sequential cascades (one per per-context Step), and the first cascade's resolution
        // causes this branch to fire mid-event — clearing the set here would empty it before the second cascade
        // arrives at the wrapper, defeating the gate. The set is kept bounded by the prune-on-add logic in
        // FireWrapperBoundaryCompletion's gate (entries with strictly earlier timestamps are pruned when a
        // new event lands), so growth is naturally limited to events from the current tick.
        UE_LOG(LogSimpleQuest, Verbose,
            TEXT("DeriveContainerLive: '%s' → not Live (no inner Steps active)"),
            *ContainerTag.ToString());
    }
    // else: container's Live state already matches what's derived — no action needed.
}

void UQuestManagerSubsystem::SetQuestResolved(FGameplayTag QuestTag, FGameplayTag OutcomeTag, EQuestResolutionSource Source)
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
    // Skipping the RemoveFact here for containers also gives loopable wrappers correct semantics — the wrapper stays Live
    // across loop iterations as long as inner Steps remain Live, instead of flickering false on each outer outcome's chain
    // processing.
    UQuestNodeBase* Node = LoadedNodeInstances.FindRef(QuestTag.GetTagName());
    const bool bIsContainer = Node && Node->IsContainerNode();

    if (!bIsContainer)
    {
        WorldState->RemoveFact(FQuestTagComposer::ResolveStateFactTag(QuestTag, EQuestStateLeaf::Live));
        
        // Step's Live just transitioned out, so its ancestor containers re-derive their Live state. Containers
        // skip this branch (they don't own a Live fact directly; their Live tracks inner Step state via the Step-side
        // ancestor walks). Wrappers being resolved as part of chain boundary processing route through this method but
        // with bIsContainer=true and don't trigger derivation.
        if (Node && Node->IsStepNode())
        {
            for (const FGameplayTag& AncestorTag : Cast<UQuestStep>(Node)->GetAncestorContainerTags())
            {
                DeriveContainerLive(AncestorTag);
            }
        }
    }
    WorldState->RemoveFact(FQuestTagComposer::ResolveStateFactTag(QuestTag, EQuestStateLeaf::PendingGiver));

    // Each SetQuestResolved call appends to the resolution registry (Layer 2 below) and bumps the WorldState
    // Completed fact's ref-count. Pre-multi-resolution, this site guarded against double-add via an
    // !IsCompleted check — that guard predates the explicit "quests resolve multiple times" semantic
    // documented in the layer comment above and erased the per-resolution count the ref-count is meant to
    // carry. Removing the guard restores the count semantic: ref-count == number of resolutions in the
    // current session for this canonical (matches Layer 2's resolution record count).
    const FGameplayTag CompletedFact = FQuestTagComposer::ResolveStateFactTag(QuestTag, EQuestStateLeaf::Completed);
    if (CompletedFact.IsValid())
    {
        WorldState->AddFact(CompletedFact);
    }
    WorldState->RemoveFact(FQuestTagComposer::ResolveStateFactTag(QuestTag, EQuestStateLeaf::PendingGiver));

    // Path fact write to WorldState removed in the Outcome/Path data-layer migration. The resolution
    // registry (UQuestStateSubsystem) is now the canonical source of truth for outcome-keyed queries
    // via HasResolvedWith. Subscribers that previously watched <Quest>.Path.<Outcome> facts now subscribe
    // to FQuestResolutionRecordedEvent on the ContextualTag channel. See RegisterEnablementWatch and
    // DeferChainToNextNodes for the subscription wiring. RecordResolution below publishes the event.

    // Layer 2: rich-record registry. Friend access only; external code can't mutate the registry,
    // but the manager writes it atomically with its own fact updates so the two layers stay consistent.
    // Append-only history: every resolution call records, supporting multi-outcome lifetimes per quest.
    if (UGameInstance* GI = GetGameInstance())
    {
        if (UQuestStateSubsystem* Registry = GI->GetSubsystem<UQuestStateSubsystem>())
        {
            const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
            Registry->RecordResolution(QuestTag, OutcomeTag, Now, Source);
        }
    }

    UE_LOG(LogSimpleQuest, Log, TEXT("SetQuestResolved: '%s' outcome='%s' source=%s"),
        *QuestTag.ToString(),
        *OutcomeTag.ToString(),
        Source == EQuestResolutionSource::External ? TEXT("External") : TEXT("Graph"));
}

void UQuestManagerSubsystem::SetQuestPendingGiver(FGameplayTag QuestTag)
{
    if (!WorldState || !QuestTag.IsValid()) return;

    const FGameplayTag PendingGiverFact = FQuestTagComposer::ResolveStateFactTag(QuestTag, EQuestStateLeaf::PendingGiver);

    // Idempotency guard, symmetric with SetQuestLive / SetQuestBlocked / SetQuestDeactivated. The ActivateNodeByTag
    // short-circuit (top of activation) already intercepts a second cascade arriving while PendingGiver is asserted,
    // so this is belt-and-braces, but keeping the Set* methods uniformly idempotent means any future caller reaching
    // here can't accidentally bump the ref-count past 1 on a state that's semantically a boolean.
    if (FQuestLifecycleQuery::IsPendingGiver(WorldState, QuestTag))
    {
        UE_LOG(LogSimpleQuest, Verbose, TEXT("SetQuestPendingGiver: '%s' already pending, skipping"), *QuestTag.ToString());
        return;
    }

    WorldState->AddFact(PendingGiverFact);
    UE_LOG(LogSimpleQuest, Verbose, TEXT("SetQuestPendingGiver: '%s'"), *QuestTag.ToString());

    // Ancestor walk for Steps. Container Live derives off any-inner-step-active (Live or PendingGiver), so a Step
    // entering PendingGiver propagates upward: each ancestor container re-derives its Live fact based on whether
    // any inner Step is now active. Mirrors SetQuestLive's pattern. Without this walk, a giver-gated inner Step's
    // ancestor containers stay incorrectly inactive in debug surfaces (halos, prereq examiner) until a Step
    // transitions all the way to Live. Containers don't reach here themselves; they don't own a PendingGiver fact.
    if (UQuestNodeBase* Node = LoadedNodeInstances.FindRef(QuestTag.GetTagName()))
    {
        if (Node->IsStepNode())
        {
            for (const FGameplayTag& AncestorTag : Cast<UQuestStep>(Node)->GetAncestorContainerTags())
            {
                DeriveContainerLive(AncestorTag);
            }
        }
    }
}

void UQuestManagerSubsystem::ClearQuestPendingGiver(FGameplayTag QuestTag)
{
    if (WorldState && QuestTag.IsValid())
    {
        WorldState->RemoveFact(FQuestTagComposer::ResolveStateFactTag(QuestTag, EQuestStateLeaf::PendingGiver));
        UE_LOG(LogSimpleQuest, Verbose, TEXT("ClearQuestPendingGiver: '%s'"), *QuestTag.ToString());
    }
}

void UQuestManagerSubsystem::FireWrapperBoundaryCompletion(const FQuestBoundaryCompletion& BC,
    const FOriginatingEventID& OriginatingEventID)
{
    const FGameplayTag WrapperTag = UGameplayTagsManager::Get().RequestGameplayTag(BC.WrapperTagName, false);
    if (!WrapperTag.IsValid()) return;

    if (UQuestNodeBase* WrapperNode = LoadedNodeInstances.FindRef(BC.WrapperTagName))
    {
        // Event-keyed dedup gate: a single gameplay event (Step resolution → cascade → wrapper completion)
        // can reach the same wrapper through multiple paths under multi-tag fanout — e.g., both this
        // context's Listener and another context's Listener forwarding their BoundaryCompletions to this
        // wrapper after their respective Setters publish on the shared GroupTag channel. Without this gate,
        // the wrapper resolves once per arriving cascade, doubling (or N-tupling) records for one logical event.
        // With the gate, the second arrival with a matching event ID is recognized as already-handled and
        // skipped; loops that re-resolve at a later moment (different timestamp) or multi-resolution within
        // a single Live phase from a different originating Step (different authored GUID) produce distinct
        // event IDs and proceed normally. Invalid event IDs (default-constructed — non-cascade origin like
        // direct external API resolution) skip the dedup logic entirely so non-cascade paths aren't filtered.
        if (UQuest* WrapperContainer = Cast<UQuest>(WrapperNode); WrapperContainer && OriginatingEventID.IsValid())
        {
            if (WrapperContainer->ResolvedByEvents.Contains(OriginatingEventID))
            {
                UE_LOG(LogSimpleQuest, Verbose,
                    TEXT("FireWrapperBoundaryCompletion: skipping '%s' outcome='%s' — already resolved by event guid=%s ts=%.3f"),
                    *WrapperTag.ToString(), *BC.OutcomeTag.ToString(),
                    *OriginatingEventID.AuthoredNodeGuid.ToString(EGuidFormats::Short),
                    OriginatingEventID.ResolutionTimestamp);
                return;
            }
            
            // Prune entries with strictly earlier timestamps before adding the new one. Within one logical
            // event's multi-tag fanout every cascade shares the same ResolutionTimestamp; entries with older
            // timestamps belong to events from prior ticks that cannot recur (the same authored Step at the
            // same world time would produce an identical entry already in the set, caught by the Contains
            // check above). Keeps the set bounded to events from the current tick — typically 1 entry,
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

        UE_LOG(LogSimpleQuest, Verbose,
            TEXT("FireWrapperBoundaryCompletion: routing '%s' outcome='%s' through wrapper's ChainToNextNodes (eventGuid=%s)"),
            *WrapperTag.ToString(), *BC.OutcomeTag.ToString(),
            *OriginatingEventID.AuthoredNodeGuid.ToString(EGuidFormats::Short));
        ChainToNextNodes(WrapperNode, BC.OutcomeTag, BC.OutcomeTag.GetTagName(), OriginatingEventID);
    }
    else
    {
        UE_LOG(LogSimpleQuest, Warning,
            TEXT("FireWrapperBoundaryCompletion: wrapper '%s' instance not loaded — falling back to direct SetQuestResolved + publish"),
            *WrapperTag.ToString());
        SetQuestResolved(WrapperTag, BC.OutcomeTag, EQuestResolutionSource::Graph);
    }
}

void UQuestManagerSubsystem::PublishGraphResolutions(const TArray<FGameplayTag>& GraphTags, FGameplayTag OutcomeTag,
    EQuestResolutionSource Source) const
{
    if (GraphTags.IsEmpty()) return;

    UQuestStateSubsystem* StateSubsystem = GetGameInstance() ? GetGameInstance()->GetSubsystem<UQuestStateSubsystem>() : nullptr;
    if (!StateSubsystem) return;

    const double ResolutionTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
    for (const FGameplayTag& GraphTag : GraphTags)
    {
        if (GraphTag.IsValid())
        {
            UE_LOG(LogSimpleQuest, Verbose, TEXT("PublishGraphResolutions: '%s' outcome='%s'"),
                *GraphTag.ToString(),
                *OutcomeTag.ToString());
            StateSubsystem->RecordResolution(GraphTag, OutcomeTag, ResolutionTime, Source);
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

    UE_LOG(LogSimpleQuest, Verbose, TEXT("RegisterEnablementWatch: '%s' subscribed to %d channel(s), initial satisfied=%d"),
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

    // Compute full status (with leaf detail) — we both push it to the state subsystem and use the bSatisfied
    // bit for the transition check.
    const FQuestPrereqStatus NewStatus = Instance->PrerequisiteExpression.EvaluateWithLeafStatus(WorldState, QuestStateSubsystem);

    // Push to state subsystem regardless of transition — the cache should always reflect current evaluation
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
    FQuestEventContext Context = AssembleEventContext(Instance, FQuestObjectiveContext());

    if (NewStatus.bSatisfied)
    {
        UE_LOG(LogSimpleQuest, Log, TEXT("ReevaluateEnablementWatch: '%s' — prereqs satisfied, publishing Enabled"),
            *QuestTag.ToString());
        FQuestPublish::OnAllNodeTags(QuestSignalSubsystem, Instance, FQuestEnabledEvent(QuestTag, Context));
    }
    else
    {
        UE_LOG(LogSimpleQuest, Log, TEXT("ReevaluateEnablementWatch: '%s' — prereqs no longer satisfied, publishing Disabled"),
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
