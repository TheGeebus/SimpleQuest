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
#if WITH_EDITOR
#include "Components/QuestGiverComponent.h"
#else
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#endif

void UQuestManagerSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

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
    if (const UWorld* World = GetWorld())
    {
        World->GetTimerManager().SetTimerForNextTick(this, &UQuestManagerSubsystem::StartInitialQuests);
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
            for (auto& Item : Pair.Value)
            {
                QuestSignalSubsystem->UnsubscribeMessage(Item.Key, Item.Value);
            }
        }
        DeferredCompletionPrereqHandles.Reset();

        for (auto& Pair : EnablementWatchHandles)
        {
            for (auto& Item : Pair.Value)
            {
                QuestSignalSubsystem->UnsubscribeMessage(Item.Key, Item.Value.AddedHandle);
                if (Item.Value.RemovedHandle.IsValid())
                {
                    QuestSignalSubsystem->UnsubscribeMessage(Item.Key, Item.Value.RemovedHandle);
                }
            }
        }
        EnablementWatchHandles.Reset();
        EnablementWatches.Reset();
        RecentGiverActors.Reset();
    }
    Super::Deinitialize();
}

void UQuestManagerSubsystem::StartInitialQuests_Implementation()
{
    if (!InitialQuestlines.IsEmpty())
    {
        for (const TSoftObjectPtr<UQuestlineGraph>& GraphPtr : InitialQuestlines)
        {
            if (UQuestlineGraph* Graph = GraphPtr.LoadSynchronous())
            {
                ActivateQuestlineGraph(Graph);
            }
            else
            {
                UE_LOG(LogSimpleQuest, Warning, TEXT("UQuestManagerSubsystem::StartInitialQuestlines : failed to load questline graph asset"));
            }
        }
    }
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

void UQuestManagerSubsystem::ActivateQuestlineGraph(UQuestlineGraph* Graph)
{
    if (!Graph) return;

    for (const auto& Pair : Graph->GetCompiledNodes())
    {
        if (UQuestNodeBase* Instance = Pair.Value)
        {
            // Compiled node instances live on the UQuestlineGraph asset and persist across PIE sessions. Wipe any
            // state the prior session left on them — subscription handles to a dead SignalSubsystem, deferred
            // contextual tags, activation scratch, completion snapshots — so this session starts clean.
            Instance->ResetTransientState();

            if (!Pair.Key.ToString().StartsWith(TEXT("Util_")))
            {
                Instance->ResolveQuestTag(Pair.Key);
            }
            LoadedNodeInstances.Add(Pair.Key, Instance);
            Instance->RegisterWithGameInstance(GetGameInstance());
            Instance->OnNodeCompleted.BindDynamic(this, &UQuestManagerSubsystem::HandleOnNodeCompleted);
            Instance->OnNodeStarted.BindDynamic(this, &UQuestManagerSubsystem::HandleOnNodeStarted);
            Instance->OnNodeForwardActivated.BindDynamic(this, &UQuestManagerSubsystem::HandleOnNodeForwardActivated);
            const FGameplayTag ResolvedTag = Instance->GetQuestTag();
            if (ResolvedTag.IsValid() && QuestSignalSubsystem)
            {
                FDelegateHandle Handle = QuestSignalSubsystem->SubscribeMessage<FQuestDeactivatedEvent>(ResolvedTag, this, &UQuestManagerSubsystem::HandleNodeDeactivatedEvent);
                DeactivationSubscriptionHandles.Add(ResolvedTag, Handle);
            }
            if (UQuestStep* Step = Cast<UQuestStep>(Instance))
            {
                Step->OnNodeProgress.BindDynamic(this, &UQuestManagerSubsystem::HandleOnNodeProgress);
            }
        }
    }
    UE_LOG(LogSimpleQuest, Log, TEXT("ActivateQuestlineGraph: '%s' — loaded %d node(s), activating %d entry tag(s)"),
        *Graph->GetName(), Graph->GetCompiledNodes().Num(), Graph->GetEntryNodeTags().Num());
    
    for (const FName& EntryTagName : Graph->GetEntryNodeTags())
    {
        ActivateNodeByTag(EntryTagName);
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
        *Node->GetQuestTag().ToString(), *OutcomeTag.ToString(), *PathIdentity.ToString());

    UQuestStep* Step = Cast<UQuestStep>(Node);
    if (Step
        && !Step->IsGiverGated()
        && Step->GetPrerequisiteGateMode() == EPrerequisiteGateMode::GatesCompletion
        && !Step->PrerequisiteExpression.IsAlways()
        && !Step->PrerequisiteExpression.Evaluate(WorldState, QuestStateSubsystem))
    {
        UE_LOG(LogSimpleQuest, Verbose, TEXT("HandleOnNodeCompleted: '%s' — prereqs unmet, deferring chain"), *Node->GetQuestTag().ToString());
        DeferChainToNextNodes(Step, OutcomeTag, PathIdentity);
        return;
    }

    ChainToNextNodes(Node, OutcomeTag, PathIdentity);
}

void UQuestManagerSubsystem::HandleOnNodeProgress(UQuestStep* Step, FQuestObjectiveContext ProgressData)
{
    if (!Step || !QuestSignalSubsystem) return;

    UE_LOG(LogSimpleQuest, Verbose, TEXT("HandleOnNodeProgress: '%s' — %d/%d"),
        *Step->GetQuestTag().ToString(),
        ProgressData.CurrentCount,
        ProgressData.RequiredCount);

    FQuestEventContext Context = AssembleEventContext(Step, ProgressData);
    QuestSignalSubsystem->PublishMessage(Step->GetQuestTag(), FQuestProgressEvent(Step->GetQuestTag(), Context));
}

void UQuestManagerSubsystem::HandleOnNodeStarted(UQuestNodeBase* Node, FGameplayTag InContextualTag)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(UQuestManagerSubsystem_HandleOnNodeStarted);

    if (Node->GetQuestTag().IsValid())
    {
        SetQuestLive(Node->GetQuestTag());
    }
    if (QuestSignalSubsystem)
    {
        FQuestEventContext Context = AssembleEventContext(Node, FQuestObjectiveContext());
        AActor* GiverActor = nullptr;
        
        if (TWeakObjectPtr<AActor>* Found = RecentGiverActors.Find(Node->GetQuestTag()))
        {
            GiverActor = Found->Get();
            RecentGiverActors.Remove(Node->GetQuestTag());
        }
        QuestSignalSubsystem->PublishMessage(Node->GetQuestTag(), FQuestStartedEvent(Node->GetQuestTag(), Context, GiverActor));
        
        if (UQuestStep* Step = Cast<UQuestStep>(Node))
        {
            FDelegateHandle Handle = QuestSignalSubsystem->SubscribeRawMessage<FQuestObjectiveTriggered>(Node->GetQuestTag(), this, &UQuestManagerSubsystem::CheckQuestObjectives);
            LiveStepTriggerHandles.Add(Node->GetQuestTag(), Handle);
            if (!Step->GetTargetClasses().IsEmpty())
            {
                for (const TSoftClassPtr<AActor>& SoftClass : Step->GetTargetClasses())
                {
                    // LoadSynchronous at step activation — pay the load cost once per target class when the step goes live,
                    // keep runtime event-dispatch checks fast by caching the loaded UClass in ClassFilteredSteps (TMultiMap<FGameplayTag, UClass*>).
                    if (UClass* Loaded = SoftClass.LoadSynchronous())
                    {
                        ClassFilteredSteps.Add(Node->GetQuestTag(), Loaded);
                    }
                }

                // Subscribe once to global channel if this is the first class-filtered step
                if (!ClassBridgeHandle.IsValid())
                {
                    ClassBridgeHandle = QuestSignalSubsystem->SubscribeRawMessage<FQuestObjectiveTriggered>(Tag_Channel_QuestTarget, this, &UQuestManagerSubsystem::CheckClassObjectives);
                }
            }
        }
    }
    // UQuest inner-entry activation. When Activate defers due to unmet prereqs, this branch doesn't run; when prereqs
    // satisfy (immediately or via TryActivateDeferred firing), ActivateInternal runs, OnNodeStarted fires, this branch
    // runs and drains the per-cascade queue populated by ActivateNodeByTag.
    if (UQuest* QuestNode = Cast<UQuest>(Node))
    {
        const FName NodeTagName = Node->GetQuestTag().GetTagName();

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
        if (QuestNode->GetQuestTag().IsValid())
        {
            AnyOutcomeChain.Add(QuestNode->GetQuestTag());
        }

        auto StampWithParams = [this, &QuestNode](const FName& DestTagName,
            const FQuestObjectiveActivationParams& Params, const TArray<FGameplayTag>& Chain)
        {
            if (UQuestNodeBase* DestInstance = LoadedNodeInstances.FindRef(DestTagName))
            {
                DestInstance->PendingActivationParams = Params;
                DestInstance->PendingActivationParams.OriginTag = QuestNode->GetQuestTag();
                DestInstance->PendingActivationParams.OriginChain = Chain;
            }
        };

        // Always-activate Any-Outcome entries. Fire ONCE per OnNodeStarted, not per cascade.
        for (const FName& StepTag : QuestNode->GetEntryStepTags())
        {
            StampWithParams(StepTag, FirstCascade, AnyOutcomeChain);
            ActivateNodeByTag(StepTag);
        }

        // Per-cascade outcome-specific routing. Each queued cascade fires its own entry routes — this is the
        // path that fan-in convergence patterns rely on (Q1's Victory and Q2's Defeat both routing into separate
        // inner steps when the Quest's prereq finally satisfies).
        for (const FQuestObjectiveActivationParams& CascadeParams : DrainedCascades)
        {
            const FGameplayTag IncomingOutcomeTag = CascadeParams.IncomingOutcomeTag;
            const FName IncomingSourceTag = CascadeParams.OriginTag.IsValid()
                ? CascadeParams.OriginTag.GetTagName()
                : NAME_None;

            // Write entry outcome fact for this cascade's outcome (per-cascade — different cascades may carry
            // different outcomes, each writing its own entry path fact for inner-graph prereqs to read).
            if (IncomingOutcomeTag.IsValid() && WorldState)
            {
                const FName EntryFactName = FQuestTagComposer::MakeEntryPathFact(NodeTagName, IncomingOutcomeTag.GetTagName());
                if (!EntryFactName.IsNone())
                {
                    UE_LOG(LogSimpleQuest, Verbose, TEXT("HandleOnNodeStarted: setting entry outcome fact '%s' for quest '%s'"),
                        *EntryFactName.ToString(), *NodeTagName.ToString());
                    WorldState->AddFact(UGameplayTagsManager::Get().RequestGameplayTag(EntryFactName, false));
                }
            }

            // Build chain for this cascade.
            TArray<FGameplayTag> InnerForwardChain = CascadeParams.OriginChain;
            if (QuestNode->GetQuestTag().IsValid())
            {
                InnerForwardChain.Add(QuestNode->GetQuestTag());
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
                            ActivateNodeByTag(Entry.DestTag);
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
                            ActivateNodeByTag(Entry.DestTag);
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
    for (const FName& Tag : Node->GetNextNodesOnForward())
        ActivateNodeByTag(Tag);
}

void UQuestManagerSubsystem::ActivateNodeByTag(FName NodeTagName, FGameplayTag IncomingOutcomeTag, FName IncomingSourceTag)
{

    TRACE_CPUPROFILER_EVENT_SCOPE(UQuestManagerSubsystem_ActivateNodeByTag);

    TObjectPtr<UQuestNodeBase>* InstancePtr = LoadedNodeInstances.Find(NodeTagName);
    if (!InstancePtr || !*InstancePtr)
    {
        UE_LOG(LogSimpleQuest, Warning, TEXT("UQuestManagerSubsystem::ActivateNodeByTag : no instance found for tag name '%s'"), *NodeTagName.ToString());
        return;
    }

    const FGameplayTag NodeTag = UGameplayTagsManager::Get().RequestGameplayTag(NodeTagName, false);

    /**
     * Diamond convergence guard: refuses activation if the node is already running, waiting for a giver, or explicitly blocked.
     * Completed is intentionally excluded so loops and repeatable quests can re-enter.
     */
    if (NodeTag.IsValid() && WorldState)
    {
        if (WorldState->HasFact(FQuestTagComposer::ResolveStateFactTag(NodeTag, EQuestStateLeaf::Live)) ||
            WorldState->HasFact(FQuestTagComposer::ResolveStateFactTag(NodeTag, EQuestStateLeaf::PendingGiver)) ||
            WorldState->HasFact(FQuestTagComposer::ResolveStateFactTag(NodeTag, EQuestStateLeaf::Blocked)))
        {
            /**
             * Already-live Quest receiving a late outcome: deliver the outcome without re-activating. This handles graphs where
             * a Quest node is activated before its incoming outcome arrives. Source filtering applies the same way as first-time activation.
             */
            if (IncomingOutcomeTag.IsValid() && WorldState->HasFact(FQuestTagComposer::ResolveStateFactTag(NodeTag, EQuestStateLeaf::Live)))
            {
                if (UQuest* QuestNode = Cast<UQuest>(*InstancePtr))
                {
                    UE_LOG(LogSimpleQuest, Log, TEXT("ActivateNodeByTag: delivering late outcome '%s' (source '%s') to already-live quest '%s'"),
                        *IncomingOutcomeTag.ToString(), *IncomingSourceTag.ToString(), *NodeTagName.ToString());

                    const FName EntryFactName = FQuestTagComposer::MakeEntryPathFact(NodeTagName, IncomingOutcomeTag.GetTagName());
                    if (!EntryFactName.IsNone())
                    {
                        WorldState->AddFact(UGameplayTagsManager::Get().RequestGameplayTag(EntryFactName, false));
                    }

                    if (const FQuestEntryRouteList* RouteList = QuestNode->GetEntryStepTagsByPath().Find(IncomingOutcomeTag.GetTagName()))
                    {
                        for (const FQuestEntryDestination& Entry : RouteList->Destinations)
                        {
                            if (Entry.SourceFilter == IncomingSourceTag) ActivateNodeByTag(Entry.DestTag);
                        }
                    }

                    if (IncomingSourceTag != NAME_None)
                    {
                        if (const FQuestEntryRouteList* AnyRouteList = QuestNode->GetEntryStepTagsByPath().Find(NAME_None))
                        {
                            for (const FQuestEntryDestination& Entry : AnyRouteList->Destinations)
                            {
                                if (Entry.SourceFilter == IncomingSourceTag) ActivateNodeByTag(Entry.DestTag);
                            }
                        }
                    }
                }
            }
            UE_LOG(LogSimpleQuest, Verbose, TEXT("ActivateNodeByTag: '%s' skipped (already %s)"),
                *NodeTagName.ToString(),
                WorldState->HasFact(FQuestTagComposer::ResolveStateFactTag(NodeTag, EQuestStateLeaf::Live)) ? TEXT("live") :
                WorldState->HasFact(FQuestTagComposer::ResolveStateFactTag(NodeTag, EQuestStateLeaf::PendingGiver)) ? TEXT("pending giver") :
                TEXT("blocked"));
            return;
        }
        // Clear Deactivated if present. A deactivated node is allowed to re-enter via its Activate input.
        WorldState->RemoveFact(FQuestTagComposer::ResolveStateFactTag(NodeTag, EQuestStateLeaf::Deactivated));
    }

    if (NodeTag.IsValid() && RegisteredGiverQuestTags.Contains(NodeTag))
    {
        UQuestNodeBase* Instance = *InstancePtr;
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

            QuestSignalSubsystem->PublishMessage(NodeTag, FQuestActivatedEvent(NodeTag, Context, PrereqStatus));

            if (PrereqStatus.bSatisfied)
            {
                QuestSignalSubsystem->PublishMessage(NodeTag, FQuestEnabledEvent(NodeTag, Context));
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
    }

    // Cascade origin fallback: ChainToNextNodes pre-stamps OriginTag and OriginChain for every cascade destination as
    // of Piece D, so this block is effectively a no-op on the normal cascade path. Retained as a safety net for any
    // direct caller that passes IncomingSourceTag without pre-stamping. Guard on empty OriginChain so the chain
    // propagation built by ChainToNextNodes isn't stomped with a double-append.
    if (IncomingSourceTag != NAME_None)
    {
        UQuestNodeBase* Instance = *InstancePtr;
        if (Instance && Instance->PendingActivationParams.OriginChain.Num() == 0)
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
    // inner entries activate only after ActivateInternal actually fires - synchronously when prereqs are
    // satisfied immediately, or later via TryActivateDeferred when a leaf fact arrives.
    (*InstancePtr)->PendingActivationParams.IncomingOutcomeTag = IncomingOutcomeTag;

    // For UQuest containers, snapshot this cascade's params into the per-cascade queue. HandleOnNodeStarted
    // drains the queue and fires entry routes for each cascade - necessary so fan-in convergence patterns
    // (multiple upstream outcomes converging at a single deferred Quest) all route correctly when the prereq
    // satisfies. Without this snapshot, only the most-recently-stamped IncomingOutcomeTag would survive,
    // dropping earlier cascades.
    if (UQuest* QuestNode = Cast<UQuest>(*InstancePtr))
    {
        QuestNode->PendingEntryActivations.Add(QuestNode->PendingActivationParams);
    }

    (*InstancePtr)->Activate(NodeTag);
    
    UE_LOG(LogSimpleQuest, Log, TEXT("ActivateNodeByTag: '%s' activated (source '%s', outcome '%s')"),
        *NodeTagName.ToString(),
        *IncomingSourceTag.ToString(),
        *IncomingOutcomeTag.ToString());

}

void UQuestManagerSubsystem::ChainToNextNodes(UQuestNodeBase* Node, FGameplayTag OutcomeTag, FName PathIdentity)
{
    if (!Node) return;

    TRACE_CPUPROFILER_EVENT_SCOPE(UQuestManagerSubsystem_ChainToNextNodes);

    // Auto-derive PathIdentity from OutcomeTag when caller passes NAME_None. Preserves back-compat for any
    // direct C++ caller that hasn't been updated to thread PathIdentity through. Static K2 placements always
    // produce a PathIdentity matching OutcomeTag.GetTagName(), so this path is a no-op for them.
    const FName ResolvedPath = PathIdentity.IsNone() ? OutcomeTag.GetTagName() : PathIdentity;
    const TArray<FName>* PathNodes = Node->GetNextNodesForPath(ResolvedPath);
    const int32 PathCount = PathNodes ? PathNodes->Num() : 0;
    UE_LOG(LogSimpleQuest, Log, TEXT("ChainToNextNodes: '%s' outcome='%s' path='%s' - %d path + %d any-outcome downstream node(s)"),
        *Node->GetQuestTag().ToString(),
        *OutcomeTag.ToString(),
        *ResolvedPath.ToString(),
        PathCount,
        Node->GetNextNodesOnAnyOutcome().Num());

    if (Node->GetQuestTag().IsValid())
    {
        SetQuestResolved(Node->GetQuestTag(), OutcomeTag, EQuestResolutionSource::Graph);
        if (QuestSignalSubsystem)
        {
            if (FDelegateHandle* Handle = LiveStepTriggerHandles.Find(Node->GetQuestTag()))
            {
                QuestSignalSubsystem->UnsubscribeMessage(Node->GetQuestTag(), *Handle);
                LiveStepTriggerHandles.Remove(Node->GetQuestTag());
            }
        }
    }

    PublishQuestEndedEvent(Node, OutcomeTag, EQuestResolutionSource::Graph);

    /**
     * Thread this node's compiled QuestTag (as FName) forward as IncomingSourceTag so any Quest destination in the next layer
     * can filter its source-qualified entries against the originator of this outcome.
     */
    const FName SourceTagName = Node->GetQuestTag().GetTagName();

    // Piece D handoff: gather forward params from the completing step (designer-supplied via CompleteObjectiveWithOutcome)
    // and build the OriginChain extension (received chain + this step's tag) so downstream steps see the full history.
    FQuestObjectiveActivationParams ForwardPayload;
    TArray<FGameplayTag> ForwardChain;
    if (const UQuestStep* CompletingStep = Cast<UQuestStep>(Node))
    {
        ForwardPayload = CompletingStep->GetCompletionForwardParams();
        ForwardChain = CompletingStep->GetReceivedActivationParams().OriginChain;
    }
    if (Node->GetQuestTag().IsValid())
    {
        ForwardChain.Add(Node->GetQuestTag());
    }

    auto StampAndActivate = [this, &ForwardPayload, &ForwardChain, OutcomeTag, SourceTagName, &Node](const FName& DestTagName)
    {
        if (UQuestNodeBase* DestInstance = LoadedNodeInstances.FindRef(DestTagName))
        {
            DestInstance->PendingActivationParams = ForwardPayload;
            DestInstance->PendingActivationParams.OriginTag = Node->GetQuestTag();
            DestInstance->PendingActivationParams.OriginChain = ForwardChain;
        }
        ActivateNodeByTag(DestTagName, OutcomeTag, SourceTagName);
    };

    // Helper: fire one boundary completion. Resolves the wrapper's FName to its registered FGameplayTag,
    // calls SetQuestResolved (writes Completed + Path facts, appends to History), and publishes
    // FQuestEndedEvent on the wrapper tag so subscribers receive the boundary-completion event.
    auto FireBoundaryCompletion = [this](const FQuestBoundaryCompletion& BC)
    {
        const FGameplayTag WrapperTag = UGameplayTagsManager::Get().RequestGameplayTag(BC.WrapperTagName, false);
        if (!WrapperTag.IsValid()) return;

        SetQuestResolved(WrapperTag, BC.OutcomeTag, EQuestResolutionSource::Graph);

        if (UQuestNodeBase* WrapperNode = LoadedNodeInstances.FindRef(BC.WrapperTagName))
        {
            PublishQuestEndedEvent(WrapperNode, BC.OutcomeTag, EQuestResolutionSource::Graph);
        }
    };

    // Named-outcome path: fire boundary completions first so wrapper Path facts exist before the destination
    // node's prereq evaluation runs (otherwise the destination node defers waiting on a fact we're about to
    // write — defer-then-recover would also work but produces unnecessary one-tick latency).
    if (const FQuestPathNodeList* PathList = Node->GetNextNodesByPath().Find(ResolvedPath))
    {
        for (const FQuestBoundaryCompletion& BC : PathList->BoundaryCompletions)
        {
            FireBoundaryCompletion(BC);
        }
        for (const FName& Tag : PathList->NodeTags)
        {
            StampAndActivate(Tag);
        }
    }

    // Any-outcome path: same pattern.
    for (const FQuestBoundaryCompletion& BC : Node->GetBoundaryCompletionsOnAnyOutcome())
    {
        FireBoundaryCompletion(BC);
    }
    for (const FName& Tag : Node->GetNextNodesOnAnyOutcome())
    {
        StampAndActivate(Tag);
    }
}

void UQuestManagerSubsystem::SetQuestDeactivated(FGameplayTag QuestTag, EDeactivationSource Source)
{
    if (!QuestTag.IsValid() || !WorldState) return;

    if (WorldState->HasFact(FQuestTagComposer::ResolveStateFactTag(QuestTag, EQuestStateLeaf::Completed)))
    {
        UE_LOG(LogSimpleQuest, Verbose, TEXT("SetQuestDeactivated: '%s' skipped - already completed"), *QuestTag.ToString());
        return;
    }

    if (WorldState->HasFact(FQuestTagComposer::ResolveStateFactTag(QuestTag, EQuestStateLeaf::Deactivated)))
    {
        UE_LOG(LogSimpleQuest, Verbose, TEXT("SetQuestDeactivated: '%s' skipped - already deactivated"), *QuestTag.ToString());
        return;
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

	// PendingGiver cleanup
    if (WorldState->HasFact(FQuestTagComposer::ResolveStateFactTag(QuestTag, EQuestStateLeaf::PendingGiver)))
    {
        RegisteredGiverQuestTags.Remove(QuestTag);
        ClearQuestPendingGiver(QuestTag);
    }

    // Enablement watch cleanup: Defensive against entry persisting after deactivation.
    ClearEnablementWatch(QuestTag);

	// Active node cleanup: Use Node instead of redundant lookup
	if (WorldState->HasFact(FQuestTagComposer::ResolveStateFactTag(QuestTag, EQuestStateLeaf::Live)))
	{
		if (Node)
		{
			Node->DeactivateInternal(QuestTag);
		}
		WorldState->RemoveFact(FQuestTagComposer::ResolveStateFactTag(QuestTag, EQuestStateLeaf::Live));

		if (FDelegateHandle* Handle = LiveStepTriggerHandles.Find(QuestTag))
		{
			if (QuestSignalSubsystem) QuestSignalSubsystem->UnsubscribeMessage(QuestTag, *Handle);
			LiveStepTriggerHandles.Remove(QuestTag);
		}
		
		if (TMap<FGameplayTag, FDelegateHandle>* Handles = DeferredCompletionPrereqHandles.Find(QuestTag))
		{
			for (const auto& Pair : *Handles)
			{
				QuestSignalSubsystem->UnsubscribeMessage(Pair.Key, Pair.Value);
			}
			DeferredCompletionPrereqHandles.Remove(QuestTag);
		}
		DeferredCompletions.Remove(QuestTag);
	}

	WorldState->AddFact(FQuestTagComposer::ResolveStateFactTag(QuestTag, EQuestStateLeaf::Deactivated));

	// Publish with context
	if (QuestSignalSubsystem)
	{
		FQuestDeactivatedEvent Event(QuestTag, Source);
		if (Node)
		{
			Event = FQuestDeactivatedEvent(QuestTag, Source, AssembleEventContext(Node, FQuestObjectiveContext()));
		}
		QuestSignalSubsystem->PublishMessage(QuestTag, Event);
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
        ActivateNodeByTag(Tag);
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
    if (!QuestSignalSubsystem || !Node->GetQuestTag().IsValid()) return;

    FQuestObjectiveContext CompletionCtx;
    if (const UQuestStep* Step = Cast<UQuestStep>(Node))
    {
        CompletionCtx = Step->GetCompletionContext();
    }

    FQuestEventContext Context = AssembleEventContext(Node, CompletionCtx);
    QuestSignalSubsystem->PublishMessage(Node->GetQuestTag(), FQuestEndedEvent(Node->GetQuestTag(), OutcomeTag, Source, Context));
}

void UQuestManagerSubsystem::HandleGiveQuestEvent(FGameplayTag Channel, const FQuestGivenEvent& Event)
{
    const FGameplayTag QuestTag = Event.GetQuestTag();
    if (!QuestTag.IsValid()) return;

    // Blocker gate — check before mutating state. State subsystem owns blocker computation; we read its
    // result. Refused gives don't disrupt the quest's PendingGiver state.
    TArray<FQuestActivationBlocker> Blockers;
    if (UQuestStateSubsystem* StateSubsystem = GetGameInstance() ? GetGameInstance()->GetSubsystem<UQuestStateSubsystem>() : nullptr)
    {
        Blockers = StateSubsystem->QueryQuestActivationBlockers(QuestTag);
    }
    if (!Blockers.IsEmpty())
    {
        if (QuestSignalSubsystem)
        {
            QuestSignalSubsystem->PublishMessage(QuestTag, FQuestGiveBlockedEvent(QuestTag, Blockers, Event.Params.ActivationSource));
        }
        UE_LOG(LogSimpleQuest, Warning,
            TEXT("HandleGiveQuestEvent: '%s' refused — %d blocker(s) present (first reason index=%d)"),
            *QuestTag.ToString(), Blockers.Num(), static_cast<int32>(Blockers[0].Reason));
        return;
    }

    UE_LOG(LogSimpleQuest, Log, TEXT("HandleGiveQuestEvent: '%s' — clearing PendingGiver, activating (CustomData %s, ActivationSource %s)"),
        *QuestTag.ToString(),
        Event.Params.CustomData.IsValid() ? TEXT("populated") : TEXT("empty"),
        Event.Params.ActivationSource ? *Event.Params.ActivationSource->GetName() : TEXT("null"));

    if (Event.Params.ActivationSource)
    {
        RecentGiverActors.Add(QuestTag, Event.Params.ActivationSource);
    }
    
    RegisteredGiverQuestTags.Remove(QuestTag);
    ClearQuestPendingGiver(QuestTag);
    ClearEnablementWatch(QuestTag);

    // Mirror of HandleActivationRequest: stash the giver-authored params on the target step so ActivateInternal
    // merges them with the step's authored defaults. Empty Params stamps cleanly. Additive merge preserves the
    // step's defaults in that case.
    if (UQuestNodeBase* Instance = LoadedNodeInstances.FindRef(QuestTag.GetTagName()))
    {
        if (UQuestStep* Step = Cast<UQuestStep>(Instance))
        {
            Step->PendingActivationParams = Event.Params;
        }
    }

    ActivateNodeByTag(QuestTag.GetTagName());
}

void UQuestManagerSubsystem::HandleActivationRequest(FGameplayTag Channel, const FQuestActivationRequestEvent& Event)
{
    const FGameplayTag QuestTag = Event.GetQuestTag();
    if (!QuestTag.IsValid()) return;

    UE_LOG(LogSimpleQuest, Log, TEXT("HandleActivationRequest: '%s' — activating with external params (CustomData %s)"),
        *QuestTag.ToString(),
        Event.Params.CustomData.IsValid() ? TEXT("populated") : TEXT("empty"));

    // Stash params on the target step (if it IS a step) before activation, so ActivateInternal merges them with
    // the Step's authored defaults.
    if (UQuestNodeBase* Instance = LoadedNodeInstances.FindRef(QuestTag.GetTagName()))
    {
        if (UQuestStep* Step = Cast<UQuestStep>(Instance))
        {
            Step->PendingActivationParams = Event.Params;
        }
    }

    ActivateNodeByTag(QuestTag.GetTagName());
}

void UQuestManagerSubsystem::HandleBlockRequest(FGameplayTag Channel, const FQuestBlockRequestEvent& Event)
{
    const FGameplayTag QuestTag = Event.GetQuestTag();
    if (!QuestTag.IsValid() || !WorldState) return;

    const FGameplayTag BlockedFact = FQuestTagComposer::ResolveStateFactTag(QuestTag, EQuestStateLeaf::Blocked);
    if (!BlockedFact.IsValid()) return;

    // Idempotency guard: symmetric with the already-deactivated guard in SetQuestDeactivated. Spamming
    // SetQuestBlocked on an already-blocked quest would otherwise bump the WorldState ref-count and re-publish
    // the deactivation request to any third-party subscribers, neither of which reflects a genuine state
    // transition.
    if (WorldState->HasFact(BlockedFact))
    {
        UE_LOG(LogSimpleQuest, Verbose, TEXT("HandleBlockRequest: '%s' skipped — already blocked"), *QuestTag.ToString());
        return;
    }

    WorldState->AddFact(BlockedFact);

    if (QuestSignalSubsystem)
    {
        QuestSignalSubsystem->PublishMessage(Tag_Channel_QuestDeactivateRequest, FQuestDeactivateRequestEvent(QuestTag, EDeactivationSource::Internal));
    }

    UE_LOG(LogSimpleQuest, Log, TEXT("HandleBlockRequest: '%s' — Blocked fact added, deactivation requested"), *QuestTag.ToString());
}

void UQuestManagerSubsystem::HandleClearBlockRequest(FGameplayTag Channel, const FQuestClearBlockRequestEvent& Event)
{
    const FGameplayTag QuestTag = Event.GetQuestTag();
    if (!QuestTag.IsValid() || !WorldState) return;

    const FGameplayTag BlockedFact = FQuestTagComposer::ResolveStateFactTag(QuestTag, EQuestStateLeaf::Blocked);
    if (!BlockedFact.IsValid()) return;

    // Symmetric with the already-blocked guard in HandleBlockRequest. ClearFact is naturally idempotent at the
    // WorldState layer, but suppressing the "cleared" log when there's nothing to clear keeps panel observability
    // honest and avoids implying a state transition that didn't happen.
    if (!WorldState->HasFact(BlockedFact))
    {
        UE_LOG(LogSimpleQuest, Verbose, TEXT("HandleClearBlockRequest: '%s' skipped — not currently blocked"), *QuestTag.ToString());
        return;
    }

    WorldState->ClearFact(BlockedFact);
    // Deactivated intentionally not cleared: the target's Activate input clears it on re-entry.

    UE_LOG(LogSimpleQuest, Log, TEXT("HandleClearBlockRequest: '%s' — Blocked fact cleared"), *QuestTag.ToString());
}

void UQuestManagerSubsystem::HandleResolveRequest(FGameplayTag Channel, const FQuestResolveRequestEvent& Event)
{
    const FGameplayTag QuestTag = Event.GetQuestTag();
    if (!QuestTag.IsValid() || !WorldState) return;

    // Override guard — skip if already in a terminal state unless designer explicitly opts in. Default-false
    // protects against accidental double-broadcast; opt-in true appends additively (never removes prior facts).
    if (!Event.bOverrideExisting)
    {
        if (WorldState->HasFact(FQuestTagComposer::ResolveStateFactTag(QuestTag, EQuestStateLeaf::Completed))
            || WorldState->HasFact(FQuestTagComposer::ResolveStateFactTag(QuestTag, EQuestStateLeaf::Deactivated)))
        {
            UE_LOG(LogSimpleQuest, Warning,
                TEXT("HandleResolveRequest: '%s' skipped — already in terminal state. Pass bOverrideExisting=true to append a new resolution entry additively."),
                *QuestTag.ToString());
            return;
        }
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
        if (TMap<FGameplayTag, FDelegateHandle>* Handles = DeferredCompletionPrereqHandles.Find(QuestTag))
        {
            for (const auto& Pair : *Handles)
            {
                QuestSignalSubsystem->UnsubscribeMessage(Pair.Key, Pair.Value);
            }
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
            const FGameplayTag QuestTag = UGameplayTagsManager::Get().RequestGameplayTag(FName(*TagStr), false);
            if (!QuestTag.IsValid())
            {
                UE_LOG(LogSimpleQuest, Warning,
                    TEXT("UQuestManagerSubsystem::RegisterGiversFromAssetRegistry : tag '%s' is not registered — has the questline been compiled?"),
                    *TagStr);
                continue;
            }
            RegisteredGiverQuestTags.Add(QuestTag);
            UE_LOG(LogSimpleQuest, Verbose,
                TEXT("UQuestManagerSubsystem::RegisterGiversFromAssetRegistry : registered giver for '%s' from '%s' (asset registry)"),
                *QuestTag.ToString(), *Asset.AssetName.ToString());
        }
    }
#endif
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
    const FGameplayTag StepTag = Step->GetQuestTag();
    DeferredCompletions.Add(StepTag, FQuestDeferredCompletion{ OutcomeTag, PathIdentity });

    TMap<FGameplayTag, FDelegateHandle>& Handles = DeferredCompletionPrereqHandles.FindOrAdd(StepTag);
    PrereqLeafSubscription::SubscribeLeavesForReevaluation(
        Step->PrerequisiteExpression,
        this,
        &UQuestManagerSubsystem::OnDeferredCompletionPrereqAdded,
        &UQuestManagerSubsystem::OnDeferredCompletionPrereqResolutionRecorded,
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
    if (TMap<FGameplayTag, FDelegateHandle>* Handles = DeferredCompletionPrereqHandles.Find(StepTag))
    {
        for (auto& Pair : *Handles)
        {
            QuestSignalSubsystem->UnsubscribeMessage(Pair.Key, Pair.Value);
        }
        DeferredCompletionPrereqHandles.Remove(StepTag);
    }

    FQuestDeferredCompletion Pending;
    DeferredCompletions.RemoveAndCopyValue(StepTag, Pending);

    ChainToNextNodes(Step, Pending.OutcomeTag, Pending.PathIdentity);
}

void UQuestManagerSubsystem::HandleGiverRegisteredEvent(FGameplayTag Channel, const FQuestGiverRegisteredEvent& Event)
{
    const FGameplayTag QuestTag = Event.GetQuestTag();
    if (!QuestTag.IsValid()) return;

    RegisteredGiverQuestTags.Add(QuestTag);
    UE_LOG(LogSimpleQuest, Verbose, TEXT("UQuestManagerSubsystem::HandleGiverRegisteredEvent : giver registered for '%s'"), *QuestTag.ToString());

    if (WorldState && WorldState->HasFact(FQuestTagComposer::ResolveStateFactTag(QuestTag, EQuestStateLeaf::Live)))
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
    if (WorldState->HasFact(LiveFact))
    {
        UE_LOG(LogSimpleQuest, Verbose, TEXT("SetQuestLive: '%s' already live, skipping (convergence)"), *QuestTag.ToString());
        return;
    }

    UE_LOG(LogSimpleQuest, Verbose, TEXT("SetQuestLive: '%s'"), *QuestTag.ToString());
    WorldState->AddFact(LiveFact);
}

void UQuestManagerSubsystem::SetQuestResolved(FGameplayTag QuestTag, FGameplayTag OutcomeTag, EQuestResolutionSource Source)
{
    if (!WorldState || !QuestTag.IsValid()) return;

    // Layer 1: WorldState boolean-fact layer. State facts are semantically boolean ("has X been asserted?") so each
    // AddFact is guarded against ref-count duplication on convergent or repeat-resolution paths. The resolution
    // registry below (Layer 2) and any downstream chain dispatch driven by the caller are NOT gated here. Quests
    // are allowed to resolve multiple times (stays-Live-after-completion, or deactivate > reactivate > re-resolve), and
    // each fire should append to history and propagate signals normally.
    WorldState->RemoveFact(FQuestTagComposer::ResolveStateFactTag(QuestTag, EQuestStateLeaf::Live));
    WorldState->RemoveFact(FQuestTagComposer::ResolveStateFactTag(QuestTag, EQuestStateLeaf::PendingGiver));

    const FGameplayTag CompletedFact = FQuestTagComposer::ResolveStateFactTag(QuestTag, EQuestStateLeaf::Completed);
    if (CompletedFact.IsValid() && !WorldState->HasFact(CompletedFact))
    {
        WorldState->AddFact(CompletedFact);
    }

    // Path fact write to WorldState removed in the Outcome/Path data-layer migration. The resolution
    // registry (UQuestStateSubsystem) is now the canonical source of truth for outcome-keyed queries
    // via HasResolvedWith. Subscribers that previously watched <Quest>.Path.<Outcome> facts now subscribe
    // to FQuestResolutionRecordedEvent on the QuestTag channel. See RegisterEnablementWatch and
    // DeferChainToNextNodes for the subscription wiring. RecordResolution below publishes the event.
    
    /*
    if (OutcomeTag.IsValid())
    {
        // Path fact is asserted on every resolution: ref-count grows per fire, NOT idempotent. Looping Steps and
        // any downstream subscriber that tracks "this outcome happened again" rely on the fresh per-resolution
        // assertion (and on the corresponding RemoveFact path in cleanup paths) to propagate. Distinct from the
        // Completed fact above, which is genuinely one-shot per quest lifetime.
        const FGameplayTag PathFact = UGameplayTagsManager::Get().RequestGameplayTag(
            FQuestStateTagUtils::MakeNodePathFact(QuestTag.GetTagName(), OutcomeTag.GetTagName()), false);
        if (PathFact.IsValid())
        {
            WorldState->AddFact(PathFact);
        }
    }
    */

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
    if (WorldState->HasFact(PendingGiverFact))
    {
        UE_LOG(LogSimpleQuest, Verbose, TEXT("SetQuestPendingGiver: '%s' already pending, skipping"), *QuestTag.ToString());
        return;
    }

    WorldState->AddFact(PendingGiverFact);
    UE_LOG(LogSimpleQuest, Verbose, TEXT("SetQuestPendingGiver: '%s'"), *QuestTag.ToString());
}

void UQuestManagerSubsystem::ClearQuestPendingGiver(FGameplayTag QuestTag)
{
    if (WorldState && QuestTag.IsValid())
    {
        WorldState->RemoveFact(FQuestTagComposer::ResolveStateFactTag(QuestTag, EQuestStateLeaf::PendingGiver));
        UE_LOG(LogSimpleQuest, Verbose, TEXT("ClearQuestPendingGiver: '%s'"), *QuestTag.ToString());
    }
}

void UQuestManagerSubsystem::RegisterEnablementWatch(FGameplayTag QuestTag, FName NodeTagName, const FPrerequisiteExpression& Expr, bool bInitialSatisfied)
{
    if (!QuestSignalSubsystem) return;

    FEnablementWatch& Watch = EnablementWatches.FindOrAdd(QuestTag);
    Watch.NodeTagName = NodeTagName;
    Watch.bLastKnownSatisfied = bInitialSatisfied;

    TMap<FGameplayTag, PrereqLeafSubscription::FPrereqLeafHandlePair>& Handles = EnablementWatchHandles.FindOrAdd(QuestTag);
    PrereqLeafSubscription::SubscribeLeavesForReevaluation(
        Expr,
        this,
        &UQuestManagerSubsystem::OnEnablementLeafFactAdded,
        &UQuestManagerSubsystem::OnEnablementLeafFactRemoved,
        &UQuestManagerSubsystem::OnEnablementLeafResolutionRecorded,
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
        QuestSignalSubsystem->PublishMessage(QuestTag, FQuestEnabledEvent(QuestTag, Context));
    }
    else
    {
        UE_LOG(LogSimpleQuest, Log, TEXT("ReevaluateEnablementWatch: '%s' — prereqs no longer satisfied, publishing Disabled"),
            *QuestTag.ToString());
        QuestSignalSubsystem->PublishMessage(QuestTag, FQuestDisabledEvent(QuestTag, Context));
    }
}

void UQuestManagerSubsystem::ClearEnablementWatch(FGameplayTag QuestTag)
{
    if (TMap<FGameplayTag, PrereqLeafSubscription::FPrereqLeafHandlePair>* Handles = EnablementWatchHandles.Find(QuestTag))
    {
        if (QuestSignalSubsystem)
        {
            for (auto& Pair : *Handles)
            {
                // AddedHandle is set for both leaf kinds. RemovedHandle is set only for Leaf-typed (fact)
                // subscriptions. Leaf_Resolution leaves leave it invalid since resolution events are
                // append-only. Guard the second unsubscribe so we don't pass an invalid handle.
                QuestSignalSubsystem->UnsubscribeMessage(Pair.Key, Pair.Value.AddedHandle);
                if (Pair.Value.RemovedHandle.IsValid())
                {
                    QuestSignalSubsystem->UnsubscribeMessage(Pair.Key, Pair.Value.RemovedHandle);
                }
            }
        }
        EnablementWatchHandles.Remove(QuestTag);
    }
    EnablementWatches.Remove(QuestTag);

    // Clear cached prereq status. The quest is leaving giver state, the cache entry is no longer relevant.
    if (UQuestStateSubsystem* StateSubsystem = GetGameInstance() ? GetGameInstance()->GetSubsystem<UQuestStateSubsystem>() : nullptr)
    {
        StateSubsystem->ClearQuestPrereqStatus(QuestTag);
    }
}
