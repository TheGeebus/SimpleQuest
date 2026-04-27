// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Subsystems/QuestManagerSubsystem.h"
#include "Quests/QuestlineGraph.h"
#include "Quests/QuestNodeBase.h"
#include "Events/QuestEndedEvent.h"
#include "Events/QuestObjectiveTriggered.h"
#include "Events/QuestProgressEvent.h"
#include "Events/QuestStartedEvent.h"
#include "Events/QuestEnabledEvent.h"
#include "Events/AbandonQuestEvent.h"
#include "Events/QuestDeactivatedEvent.h"
#include "Signals/SignalSubsystem.h"
#include "GameplayTagsManager.h"
#include "SimpleQuestLog.h"
#include "Events/QuestActivatedEvent.h"
#include "Events/QuestActivationRequestEvent.h"
#include "Events/QuestDeactivateRequestEvent.h"
#include "Events/QuestDisabledEvent.h"
#include "Events/QuestGiveBlockedEvent.h"
#include "Events/QuestGivenEvent.h"
#include "Events/QuestGiverRegisteredEvent.h"
#include "Objectives/QuestObjective.h"
#include "Quests/Quest.h"
#include "Quests/QuestStep.h"
#include "WorldState/WorldStateSubsystem.h"
#include "Utilities/QuestStateTagUtils.h"
#include "Quests/Types/QuestEventContext.h"
#include "Quests/Types/QuestObjectiveContext.h"
#include "Settings/SimpleQuestSettings.h"
#include "StructUtils/InstancedStruct.h"
#include "Subsystems/QuestStateSubsystem.h"
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
        QuestSignalSubsystem = GameInstance->GetSubsystem<USignalSubsystem>();
        WorldState = GameInstance->GetSubsystem<UWorldStateSubsystem>();
        if (QuestSignalSubsystem)
        {
            AbandonDelegateHandle = QuestSignalSubsystem->SubscribeMessage<FAbandonQuestEvent>(Tag_Channel_QuestAbandoned, this, &UQuestManagerSubsystem::HandleAbandonQuestEvent);
            GivenDelegateHandle = QuestSignalSubsystem->SubscribeMessage<FQuestGivenEvent>(Tag_Channel_QuestGiven, this, &UQuestManagerSubsystem::HandleGiveQuestEvent);
            GiverRegisteredDelegateHandle = QuestSignalSubsystem->SubscribeMessage<FQuestGiverRegisteredEvent>(Tag_Channel_QuestGiverRegistered, this, &UQuestManagerSubsystem::HandleGiverRegisteredEvent);
            DeactivateEventDelegateHandle = QuestSignalSubsystem->SubscribeMessage<FQuestDeactivateRequestEvent>(Tag_Channel_QuestDeactivateRequest, this, &UQuestManagerSubsystem::HandleNodeDeactivationRequest);
            ActivationRequestDelegateHandle = QuestSignalSubsystem->SubscribeMessage<FQuestActivationRequestEvent>(Tag_Channel_QuestActivationRequest, this, &UQuestManagerSubsystem::HandleActivationRequest);
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
        QuestSignalSubsystem->UnsubscribeMessage(Tag_Channel_QuestAbandoned, AbandonDelegateHandle);
        QuestSignalSubsystem->UnsubscribeMessage(Tag_Channel_QuestGiven, GivenDelegateHandle);
        QuestSignalSubsystem->UnsubscribeMessage(Tag_Channel_QuestGiverRegistered, GiverRegisteredDelegateHandle);
        QuestSignalSubsystem->UnsubscribeMessage(Tag_Channel_QuestDeactivateRequest, DeactivateEventDelegateHandle);
        QuestSignalSubsystem->UnsubscribeMessage(Tag_Channel_QuestTarget, ClassBridgeHandle);
        QuestSignalSubsystem->UnsubscribeMessage(Tag_Channel_QuestActivationRequest, ActivationRequestDelegateHandle);

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
                QuestSignalSubsystem->UnsubscribeMessage(Item.Key, Item.Value.RemovedHandle);
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
        if (!Step->PrerequisiteExpression.Evaluate(WorldState)) return;
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

FQuestEventContext UQuestManagerSubsystem::AssembleEventContext(const UQuestNodeBase* Node, const FQuestObjectiveContext& InCompletionData) const
{
    FQuestEventContext Context;
    Context.NodeInfo = Node->GetNodeInfo();
    Context.CompletionData = InCompletionData;

    UE_LOG(LogSimpleQuest, Verbose, TEXT("AssembleEventContext: '%s' DisplayName='%s' CompletionData=%s"),
        *Context.NodeInfo.QuestTag.ToString(),
        *Context.NodeInfo.DisplayName.ToString(),
        Context.CompletionData.TriggeredActor ? TEXT("set") : TEXT("empty"));

    return Context;
}

void UQuestManagerSubsystem::HandleOnNodeCompleted(UQuestNodeBase* Node, FGameplayTag OutcomeTag)
{
    UE_LOG(LogSimpleQuest, Log, TEXT("HandleOnNodeCompleted: '%s' outcome='%s'"), *Node->GetQuestTag().ToString(), *OutcomeTag.ToString());    
    UQuestStep* Step = Cast<UQuestStep>(Node);
    if (Step
        && !Step->IsGiverGated()
        && Step->GetPrerequisiteGateMode() == EPrerequisiteGateMode::GatesCompletion
        && !Step->PrerequisiteExpression.IsAlways()
        && !Step->PrerequisiteExpression.Evaluate(WorldState))
    {
        UE_LOG(LogSimpleQuest, Verbose, TEXT("HandleOnNodeCompleted: '%s' — prereqs unmet, deferring chain"), *Node->GetQuestTag().ToString());
        DeferChainToNextNodes(Step, OutcomeTag);
        return;
    }

    ChainToNextNodes(Node, OutcomeTag);
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
                    ClassBridgeHandle = QuestSignalSubsystem->SubscribeRawMessage<FQuestObjectiveTriggered>(Tag_Channel_QuestTarget, this, &UQuestManagerSubsystem::CheckClassObjectives);                }
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

void UQuestManagerSubsystem::HandleAbandonQuestEvent(FGameplayTag Channel, const FAbandonQuestEvent& Event)
{
    const FGameplayTag QuestTag = Event.GetQuestTag();
    if (!QuestTag.IsValid()) return;
    SetQuestDeactivated(QuestTag, EDeactivationSource::External);
}

void UQuestManagerSubsystem::ActivateNodeByTag(FName NodeTagName, FGameplayTag IncomingOutcomeTag, FName IncomingSourceTag)
{
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
        if (WorldState->HasFact(MakeQuestStateFact(NodeTag, FQuestStateTagUtils::Leaf_Live)) ||
            WorldState->HasFact(MakeQuestStateFact(NodeTag, FQuestStateTagUtils::Leaf_PendingGiver)) ||
            WorldState->HasFact(MakeQuestStateFact(NodeTag, FQuestStateTagUtils::Leaf_Blocked)))
        {
            /**
             * Already-live Quest receiving a late outcome: deliver the outcome without re-activating. This handles graphs where
             * a Quest node is activated before its incoming outcome arrives. Source filtering applies the same way as first-time activation.
             */
            if (IncomingOutcomeTag.IsValid() && WorldState->HasFact(MakeQuestStateFact(NodeTag, FQuestStateTagUtils::Leaf_Live)))
            {
                if (UQuest* QuestNode = Cast<UQuest>(*InstancePtr))
                {
                    UE_LOG(LogSimpleQuest, Log, TEXT("ActivateNodeByTag: delivering late outcome '%s' (source '%s') to already-live quest '%s'"),
                        *IncomingOutcomeTag.ToString(), *IncomingSourceTag.ToString(), *NodeTagName.ToString());

                    const FName EntryFactName = FQuestStateTagUtils::MakeEntryOutcomeFact(NodeTagName, IncomingOutcomeTag);
                    if (!EntryFactName.IsNone())
                    {
                        WorldState->AddFact(UGameplayTagsManager::Get().RequestGameplayTag(EntryFactName, false));
                    }

                    if (const FQuestEntryRouteList* RouteList = QuestNode->GetEntryStepTagsByOutcome().Find(IncomingOutcomeTag))
                    {
                        for (const FQuestEntryDestination& Entry : RouteList->Destinations)
                        {
                            if (Entry.SourceFilter == IncomingSourceTag) ActivateNodeByTag(Entry.DestTag);
                        }
                    }

                    if (IncomingSourceTag != NAME_None)
                    {
                        if (const FQuestEntryRouteList* AnyRouteList = QuestNode->GetEntryStepTagsByOutcome().Find(FGameplayTag()))
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
                WorldState->HasFact(MakeQuestStateFact(NodeTag, FQuestStateTagUtils::Leaf_Live)) ? TEXT("live") :
                WorldState->HasFact(MakeQuestStateFact(NodeTag, FQuestStateTagUtils::Leaf_PendingGiver)) ? TEXT("pending giver") :
                TEXT("blocked"));
            return;
        }
        // Clear Deactivated if present. A deactivated node is allowed to re-enter via its Activate input.
        WorldState->RemoveFact(MakeQuestStateFact(NodeTag, FQuestStateTagUtils::Leaf_Deactivated));
    }

    if (NodeTag.IsValid() && RegisteredGiverQuestTags.Contains(NodeTag))
    {
        UQuestNodeBase* Instance = *InstancePtr;
        Instance->bWasGiverGated = true;
        SetQuestPendingGiver(NodeTag);

        if (QuestSignalSubsystem)
        {
            FQuestEventContext Context = AssembleEventContext(Instance, FQuestObjectiveContext());
            const FQuestPrereqStatus PrereqStatus = Instance->PrerequisiteExpression.EvaluateWithLeafStatus(WorldState);

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

    // Cascade origin fallback: ChainToNextNodes pre-stamps OriginTag + OriginChain for every cascade destination as
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

    (*InstancePtr)->Activate(NodeTag);
    UE_LOG(LogSimpleQuest, Log, TEXT("ActivateNodeByTag: '%s' activated (source '%s', outcome '%s')"),
        *NodeTagName.ToString(), *IncomingSourceTag.ToString(), *IncomingOutcomeTag.ToString());

    if (UQuest* QuestNode = Cast<UQuest>(*InstancePtr))
    {
        // Write entry outcome fact for prerequisite expressions within the inner graph.
        if (IncomingOutcomeTag.IsValid() && WorldState)
        {
            const FName EntryFactName = FQuestStateTagUtils::MakeEntryOutcomeFact(NodeTagName, IncomingOutcomeTag);
            if (!EntryFactName.IsNone())
            {
                UE_LOG(LogSimpleQuest, Verbose, TEXT("ActivateNodeByTag: setting entry outcome fact '%s' for quest '%s'"),
                    *EntryFactName.ToString(), *NodeTagName.ToString());
                WorldState->AddFact(UGameplayTagsManager::Get().RequestGameplayTag(EntryFactName, false));
            }
        }

        // Quest-boundary origin forwarding: take the chain that was stamped on the Quest by whoever cascaded into it
        // (outer ChainToNextNodes, a Piece B/C event, or a manual ActivateNodeByTag call), extend it with this Quest's
        // own tag, and pre-stamp every inner entry destination so they receive the full history rather than starting
        // fresh. Consume + clear so re-entering the Quest later starts from a fresh stamp.
        FQuestObjectiveActivationParams InnerForwardPayload = QuestNode->PendingActivationParams;
        TArray<FGameplayTag> InnerForwardChain = InnerForwardPayload.OriginChain;
        if (QuestNode->GetQuestTag().IsValid())
        {
            InnerForwardChain.Add(QuestNode->GetQuestTag());
        }
        QuestNode->PendingActivationParams = FQuestObjectiveActivationParams{};

        auto StampInnerEntry = [this, &InnerForwardPayload, &InnerForwardChain, &QuestNode](const FName& DestTagName)
        {
            if (UQuestNodeBase* DestInstance = LoadedNodeInstances.FindRef(DestTagName))
            {
                DestInstance->PendingActivationParams = InnerForwardPayload;
                DestInstance->PendingActivationParams.OriginTag = QuestNode->GetQuestTag();
                DestInstance->PendingActivationParams.OriginChain = InnerForwardChain;
            }
        };

        // Always activate the Any-Outcome entry paths — unconditional, no source filter.
        for (const FName& StepTag : QuestNode->GetEntryStepTags())
        {
            StampInnerEntry(StepTag);
            ActivateNodeByTag(StepTag);
        }

        /**
         * Outcome-specific, source-filtered entries. Each destination fires only when its SourceFilter matches the IncomingSourceTag.
         * This is where the per-source routing protocol discriminates two specs producing the same outcome from different sources.
         */
        if (IncomingOutcomeTag.IsValid())
        {
            if (const FQuestEntryRouteList* RouteList = QuestNode->GetEntryStepTagsByOutcome().Find(IncomingOutcomeTag))
            {
                for (const FQuestEntryDestination& Entry : RouteList->Destinations)
                {
                    if (Entry.SourceFilter == IncomingSourceTag)
                    {
                        StampInnerEntry(Entry.DestTag);
                        ActivateNodeByTag(Entry.DestTag);
                    }
                }
            }
        }

        /**
         * Any-outcome-from-source entries — bucket keyed by invalid FGameplayTag. Fires when the incoming source matches,
         * regardless of which specific outcome triggered entry. Gated on IncomingSourceTag so we don't dispatch any-outcome
         * routes when no source context exists (e.g., synthetic activation paths).
         */
        if (IncomingSourceTag != NAME_None)
        {
            if (const FQuestEntryRouteList* AnyRouteList = QuestNode->GetEntryStepTagsByOutcome().Find(FGameplayTag()))
            {
                for (const FQuestEntryDestination& Entry : AnyRouteList->Destinations)
                {
                    if (Entry.SourceFilter == IncomingSourceTag)
                    {
                        StampInnerEntry(Entry.DestTag);
                        ActivateNodeByTag(Entry.DestTag);
                    }
                }
            }
        }
    }
}

void UQuestManagerSubsystem::ChainToNextNodes(UQuestNodeBase* Node, FGameplayTag OutcomeTag)
{
    if (!Node) return;

    const int32 OutcomeCount = Node->GetNextNodesForOutcome(OutcomeTag) ? Node->GetNextNodesForOutcome(OutcomeTag)->Num() : 0;
    UE_LOG(LogSimpleQuest, Log, TEXT("ChainToNextNodes: '%s' outcome='%s' — %d outcome + %d any-outcome downstream node(s)"),
        *Node->GetQuestTag().ToString(), *OutcomeTag.ToString(), OutcomeCount, Node->GetNextNodesOnAnyOutcome().Num());

    if (Node->GetQuestTag().IsValid())
    {
        SetQuestResolved(Node->GetQuestTag(), OutcomeTag);
        if (QuestSignalSubsystem)
        {
            if (FDelegateHandle* Handle = LiveStepTriggerHandles.Find(Node->GetQuestTag()))
            {
                QuestSignalSubsystem->UnsubscribeMessage(Node->GetQuestTag(), *Handle);
                LiveStepTriggerHandles.Remove(Node->GetQuestTag());
            }
        }
    }

    PublishQuestEndedEvent(Node, OutcomeTag);

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

    if (const TArray<FName>* OutcomeNodes = Node->GetNextNodesForOutcome(OutcomeTag))
    {
        for (const FName& Tag : *OutcomeNodes)
        {
            StampAndActivate(Tag);
        }
    }
    for (const FName& Tag : Node->GetNextNodesOnAnyOutcome())
    {
        StampAndActivate(Tag);
    }
}

void UQuestManagerSubsystem::SetQuestDeactivated(FGameplayTag QuestTag, EDeactivationSource Source)
{
	if (!QuestTag.IsValid() || !WorldState) return;

	if (WorldState->HasFact(MakeQuestStateFact(QuestTag, FQuestStateTagUtils::Leaf_Completed)))
	{
		UE_LOG(LogSimpleQuest, Verbose, TEXT("SetQuestDeactivated: '%s' skipped — already completed"), *QuestTag.ToString());
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
    if (WorldState->HasFact(MakeQuestStateFact(QuestTag, FQuestStateTagUtils::Leaf_PendingGiver)))
    {
        RegisteredGiverQuestTags.Remove(QuestTag);
        ClearQuestPendingGiver(QuestTag);
    }

    // Enablement watch cleanup: Defensive against entry persisting after deactivation.
    ClearEnablementWatch(QuestTag);

	// Active node cleanup: Use Node instead of redundant lookup
	if (WorldState->HasFact(MakeQuestStateFact(QuestTag, FQuestStateTagUtils::Leaf_Live)))
	{
		if (Node)
		{
			Node->DeactivateInternal(QuestTag);
		}
		WorldState->RemoveFact(MakeQuestStateFact(QuestTag, FQuestStateTagUtils::Leaf_Live));

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
		DeferredCompletionOutcomes.Remove(QuestTag);
	}

	WorldState->AddFact(MakeQuestStateFact(QuestTag, FQuestStateTagUtils::Leaf_Deactivated));

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

void UQuestManagerSubsystem::PublishQuestEndedEvent(const UQuestNodeBase* Node, FGameplayTag OutcomeTag) const
{
    if (!QuestSignalSubsystem || !Node->GetQuestTag().IsValid()) return;

    FQuestObjectiveContext CompletionCtx;
    if (const UQuestStep* Step = Cast<UQuestStep>(Node))
    {
        CompletionCtx = Step->GetCompletionData();
    }

    FQuestEventContext Context = AssembleEventContext(Node, CompletionCtx);
    QuestSignalSubsystem->PublishMessage(Node->GetQuestTag(), FQuestEndedEvent(Node->GetQuestTag(), OutcomeTag, Context));
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

void UQuestManagerSubsystem::DeferChainToNextNodes(UQuestStep* Step, FGameplayTag OutcomeTag)
{
    const FGameplayTag StepTag = Step->GetQuestTag();
    DeferredCompletionOutcomes.Add(StepTag, OutcomeTag);

    TArray<FGameplayTag> LeafTags;
    Step->PrerequisiteExpression.CollectLeafTags(LeafTags);

    TMap<FGameplayTag, FDelegateHandle>& Handles = DeferredCompletionPrereqHandles.FindOrAdd(StepTag);
    for (const FGameplayTag& LeafTag : LeafTags)
    {
        FDelegateHandle Handle = QuestSignalSubsystem->SubscribeMessage<FWorldStateFactAddedEvent>(LeafTag, this, &UQuestManagerSubsystem::OnDeferredCompletionPrereqAdded);
        Handles.Add(LeafTag, Handle);
    }

    UE_LOG(LogSimpleQuest, Log, TEXT("DeferChainToNextNodes: '%s' outcome='%s' — subscribed to %d prereq leaf tag(s)"),
        *StepTag.ToString(),
        *OutcomeTag.ToString(),
        LeafTags.Num());
}

void UQuestManagerSubsystem::OnDeferredCompletionPrereqAdded(FGameplayTag Channel, const FWorldStateFactAddedEvent& Event)
{
    // Check all deferred steps. The fact that changed could satisfy any of them.
    TArray<FGameplayTag> StepTags;
    DeferredCompletionOutcomes.GetKeys(StepTags);
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
    if (!Step || !Step->PrerequisiteExpression.Evaluate(WorldState)) return;
    
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

    FGameplayTag OutcomeTag;
    DeferredCompletionOutcomes.RemoveAndCopyValue(StepTag, OutcomeTag);

    ChainToNextNodes(Step, OutcomeTag);
}

void UQuestManagerSubsystem::HandleGiverRegisteredEvent(FGameplayTag Channel, const FQuestGiverRegisteredEvent& Event)
{
    const FGameplayTag QuestTag = Event.GetQuestTag();
    if (!QuestTag.IsValid()) return;

    RegisteredGiverQuestTags.Add(QuestTag);
    UE_LOG(LogSimpleQuest, Verbose, TEXT("UQuestManagerSubsystem::HandleGiverRegisteredEvent : giver registered for '%s'"), *QuestTag.ToString());

    if (WorldState && WorldState->HasFact(MakeQuestStateFact(QuestTag, FQuestStateTagUtils::Leaf_Live)))
    {
        UE_LOG(LogSimpleQuest, Warning,
            TEXT("UQuestManagerSubsystem::HandleGiverRegisteredEvent : giver for '%s' came online after the quest already activated — gate was missed. Save the giver Blueprint to fix this for streaming scenarios."),
            *QuestTag.ToString());
    }
}

void UQuestManagerSubsystem::HandleNodeDeactivationRequest(FGameplayTag Channel, const FQuestDeactivateRequestEvent& Event)
{
    FGameplayTag EventTag = Event.GetQuestTag();
    if (EventTag.IsValid()) SetQuestDeactivated(EventTag, EDeactivationSource::Internal);
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

FGameplayTag UQuestManagerSubsystem::MakeQuestStateFact(FGameplayTag QuestTag, const FString& Leaf)
{
    const FName FactName = FQuestStateTagUtils::MakeStateFact(QuestTag, Leaf);
    return UGameplayTagsManager::Get().RequestGameplayTag(FactName, false);
}

void UQuestManagerSubsystem::SetQuestLive(FGameplayTag QuestTag)
{
    if (WorldState && QuestTag.IsValid())
    {
        UE_LOG(LogSimpleQuest, Verbose, TEXT("SetQuestLive: '%s'"), *QuestTag.ToString());
        WorldState->AddFact(MakeQuestStateFact(QuestTag, FQuestStateTagUtils::Leaf_Live));
    }
}

void UQuestManagerSubsystem::SetQuestResolved(FGameplayTag QuestTag, FGameplayTag OutcomeTag)
{
    if (!WorldState || !QuestTag.IsValid()) return;

    // Layer 1: WorldState boolean-fact layer.
    WorldState->RemoveFact(MakeQuestStateFact(QuestTag, FQuestStateTagUtils::Leaf_Live));
    WorldState->RemoveFact(MakeQuestStateFact(QuestTag, FQuestStateTagUtils::Leaf_PendingGiver));
    WorldState->AddFact(MakeQuestStateFact(QuestTag, FQuestStateTagUtils::Leaf_Completed));
    if (OutcomeTag.IsValid())
    {
        WorldState->AddFact(UGameplayTagsManager::Get().RequestGameplayTag(
            FQuestStateTagUtils::MakeNodeOutcomeFact(QuestTag.GetTagName(), OutcomeTag), false));
    }

    // Layer 2: rich-record registry. Friend access only; external code can't mutate the registry,
    // but the manager writes it atomically with its own fact updates so the two layers stay consistent.
    if (UGameInstance* GI = GetGameInstance())
    {
        if (UQuestStateSubsystem* Registry = GI->GetSubsystem<UQuestStateSubsystem>())
        {
            const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
            Registry->RecordResolution(QuestTag, OutcomeTag, Now);
        }
    }

    UE_LOG(LogSimpleQuest, Log, TEXT("SetQuestResolved: '%s' outcome='%s'"),
        *QuestTag.ToString(), *OutcomeTag.ToString());
}

void UQuestManagerSubsystem::SetQuestPendingGiver(FGameplayTag QuestTag)
{
    if (WorldState && QuestTag.IsValid())
    {
        WorldState->AddFact(MakeQuestStateFact(QuestTag, FQuestStateTagUtils::Leaf_PendingGiver));
        UE_LOG(LogSimpleQuest, Verbose, TEXT("SetQuestPendingGiver: '%s'"), *QuestTag.ToString());
    }
}

void UQuestManagerSubsystem::ClearQuestPendingGiver(FGameplayTag QuestTag)
{
    if (WorldState && QuestTag.IsValid())
    {
        WorldState->RemoveFact(MakeQuestStateFact(QuestTag, FQuestStateTagUtils::Leaf_PendingGiver));
        UE_LOG(LogSimpleQuest, Verbose, TEXT("ClearQuestPendingGiver: '%s'"), *QuestTag.ToString());
    }
}

void UQuestManagerSubsystem::RegisterEnablementWatch(FGameplayTag QuestTag, FName NodeTagName,
    const FPrerequisiteExpression& Expr, bool bInitialSatisfied)
{
    if (!QuestSignalSubsystem) return;

    FEnablementWatch& Watch = EnablementWatches.FindOrAdd(QuestTag);
    Watch.NodeTagName = NodeTagName;
    Watch.bLastKnownSatisfied = bInitialSatisfied;

    TArray<FGameplayTag> LeafTags;
    Expr.CollectLeafTags(LeafTags);

    TMap<FGameplayTag, FEnablementLeafHandles>& Handles = EnablementWatchHandles.FindOrAdd(QuestTag);
    for (const FGameplayTag& LeafTag : LeafTags)
    {
        if (!LeafTag.IsValid() || Handles.Contains(LeafTag)) continue;

        FEnablementLeafHandles LeafHandles;
        LeafHandles.AddedHandle = QuestSignalSubsystem->SubscribeMessage<FWorldStateFactAddedEvent>(
            LeafTag, this, &UQuestManagerSubsystem::OnEnablementLeafFactAdded);
        LeafHandles.RemovedHandle = QuestSignalSubsystem->SubscribeMessage<FWorldStateFactRemovedEvent>(
            LeafTag, this, &UQuestManagerSubsystem::OnEnablementLeafFactRemoved);
        Handles.Add(LeafTag, LeafHandles);
    }

    UE_LOG(LogSimpleQuest, Verbose, TEXT("RegisterEnablementWatch: '%s' subscribed to %d leaf fact(s), initial satisfied=%d"),
        *QuestTag.ToString(), LeafTags.Num(), bInitialSatisfied ? 1 : 0);
}

void UQuestManagerSubsystem::OnEnablementLeafFactAdded(FGameplayTag Channel, const FWorldStateFactAddedEvent& Event)
{
    // Iterate all watches; each re-evaluation is cheap and avoids needing inverse leaf-to-quest mapping.
    TArray<FGameplayTag> Keys;
    EnablementWatches.GetKeys(Keys);
    for (const FGameplayTag& QuestTag : Keys)
    {
        ReevaluateEnablementWatch(QuestTag);
    }
}

void UQuestManagerSubsystem::OnEnablementLeafFactRemoved(FGameplayTag Channel, const FWorldStateFactRemovedEvent& Event)
{
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
    const FQuestPrereqStatus NewStatus = Instance->PrerequisiteExpression.EvaluateWithLeafStatus(WorldState);

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
    if (TMap<FGameplayTag, FEnablementLeafHandles>* Handles = EnablementWatchHandles.Find(QuestTag))
    {
        if (QuestSignalSubsystem)
        {
            for (auto& Pair : *Handles)
            {
                QuestSignalSubsystem->UnsubscribeMessage(Pair.Key, Pair.Value.AddedHandle);
                QuestSignalSubsystem->UnsubscribeMessage(Pair.Key, Pair.Value.RemovedHandle);
            }
        }
        EnablementWatchHandles.Remove(QuestTag);
    }
    EnablementWatches.Remove(QuestTag);

    // Clear cached prereq status — the quest is leaving giver state, the cache entry is no longer relevant.
    if (UQuestStateSubsystem* StateSubsystem = GetGameInstance() ? GetGameInstance()->GetSubsystem<UQuestStateSubsystem>() : nullptr)
    {
        StateSubsystem->ClearQuestPrereqStatus(QuestTag);
    }
}
