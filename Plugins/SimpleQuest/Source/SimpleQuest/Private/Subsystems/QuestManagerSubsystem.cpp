// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Subsystems/QuestManagerSubsystem.h"
#include "Quests/QuestlineGraph.h"
#include "Quests/QuestNodeBase.h"
#include "Events/QuestEndedEvent.h"
#include "Events/QuestObjectiveTriggered.h"
#include "Events/QuestStartedEvent.h"
#include "Events/QuestEnabledEvent.h"
#include "Events/AbandonQuestEvent.h"
#include "Events/QuestDeactivatedEvent.h"
#include "Signals/SignalSubsystem.h"
#include "GameplayTagsManager.h"
#include "SimpleQuestLog.h"
#include "Events/QuestDeactivateRequestEvent.h"
#include "Events/QuestGivenEvent.h"
#include "Events/QuestGiverRegisteredEvent.h"
#include "Objectives/QuestObjective.h"
#include "Quests/Quest.h"
#include "Quests/QuestStep.h"
#include "WorldState/WorldStateSubsystem.h"
#include "Utilities/QuestStateTagUtils.h"
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
        }
    }

    RegisterGiversFromAssetRegistry();
    
    UE_LOG(LogSimpleQuest, Log, TEXT("UQuestManagerSubsystem::Initialize : Initializing: %s"), *GetFullName());
    if (const UWorld* World = GetWorld())
    {
        World->GetTimerManager().SetTimerForNextTick(this, &UQuestManagerSubsystem::StartInitialQuests);
    }
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

void UQuestManagerSubsystem::CheckQuestObjectives(FGameplayTag Channel, const FQuestObjectiveTriggered& QuestObjectiveEvent)
{
    TObjectPtr<UQuestNodeBase>* NodePtr = LoadedNodeInstances.Find(Channel.GetTagName());
    if (!NodePtr) return;

    UQuestStep* Step = Cast<UQuestStep>(*NodePtr);
    if (!Step || !Step->GetActiveObjective()) return;

    if (Step->GetPrerequisiteGateMode() == EPrerequisiteGateMode::GatesProgression
        && !Step->IsGiverGated()  // giver-gated steps already resolved prereqs at activation
        && !Step->PrerequisiteExpression.IsAlways())
    {
        if (!Step->PrerequisiteExpression.Evaluate(WorldState)) return;
    }

    Step->GetActiveObjective()->TryCompleteObjective(QuestObjectiveEvent.TriggeredActor);
}

void UQuestManagerSubsystem::ActivateQuestlineGraph(UQuestlineGraph* Graph)
{
    if (!Graph) return;

    for (const auto& Pair : Graph->GetCompiledNodes())
    {
        if (UQuestNodeBase* Instance = Pair.Value)
        {
            if (!Pair.Key.ToString().StartsWith(TEXT("Util_")))
            {
                Instance->ResolveQuestTag(Pair.Key);
            }
            LoadedNodeInstances.Add(Pair.Key, Instance);
            Instance->RegisterWithGameInstance(GetGameInstance());
            Instance->OnNodeCompleted.BindDynamic(this, &UQuestManagerSubsystem::HandleOnNodeCompleted);
            Instance->OnNodeActivated.BindDynamic(this, &UQuestManagerSubsystem::HandleOnNodeActivated);
            Instance->OnNodeForwardActivated.BindDynamic(this, &UQuestManagerSubsystem::HandleOnNodeForwardActivated);
            const FGameplayTag ResolvedTag = Instance->GetQuestTag();
            if (ResolvedTag.IsValid() && QuestSignalSubsystem)
            {
                FDelegateHandle Handle = QuestSignalSubsystem->SubscribeMessage<FQuestDeactivatedEvent>(ResolvedTag, this, &UQuestManagerSubsystem::HandleNodeDeactivatedEvent);
                DeactivationSubscriptionHandles.Add(ResolvedTag, Handle);
            }
        }
    }

    for (const FName& EntryTagName : Graph->GetEntryNodeTags())
    {
        ActivateNodeByTag(EntryTagName);
    }
}

void UQuestManagerSubsystem::HandleOnNodeCompleted(UQuestNodeBase* Node, FGameplayTag OutcomeTag)
{
    UE_LOG(LogSimpleQuest, Warning, TEXT("HandleOnNodeCompleted : Node='%s' Outcome='%s'"), *Node->GetQuestTag().ToString(), *OutcomeTag.ToString());
    
    UQuestStep* Step = Cast<UQuestStep>(Node);
    if (Step
        && !Step->IsGiverGated()
        && Step->GetPrerequisiteGateMode() == EPrerequisiteGateMode::GatesCompletion
        && !Step->PrerequisiteExpression.IsAlways()
        && !Step->PrerequisiteExpression.Evaluate(WorldState))
    {
        DeferChainToNextNodes(Step, OutcomeTag);
        return;
    }

    ChainToNextNodes(Node, OutcomeTag);
}

void UQuestManagerSubsystem::HandleOnNodeActivated(UQuestNodeBase* Node, FGameplayTag InContextualTag)
{
    if (Node->GetQuestTag().IsValid())
    {
        SetQuestActive(Node->GetQuestTag());
    }
    if (QuestSignalSubsystem)
    {
        QuestSignalSubsystem->PublishMessage(Node->GetQuestTag(), FQuestStartedEvent(Node->GetQuestTag()));
        if (UQuestStep* Step = Cast<UQuestStep>(Node))
        {
            FDelegateHandle Handle = QuestSignalSubsystem->SubscribeMessage<FQuestObjectiveTriggered>(Node->GetQuestTag(), this, &UQuestManagerSubsystem::CheckQuestObjectives);
            ActiveStepTriggerHandles.Add(Node->GetQuestTag(), Handle);
            if (!Step->GetTargetClasses().IsEmpty())
            {
                for (const TSubclassOf<AActor>& Class : Step->GetTargetClasses())
                {
                    ClassFilteredSteps.Add(Node->GetQuestTag(), Class);
                }

                // Subscribe once to global channel if this is the first class-filtered step
                if (!ClassBridgeHandle.IsValid())
                {
                    ClassBridgeHandle = QuestSignalSubsystem->SubscribeMessage<FQuestObjectiveTriggered>(Tag_Channel_QuestTarget, this, &UQuestManagerSubsystem::CheckClassObjectives);
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

void UQuestManagerSubsystem::HandleAbandonQuestEvent(FGameplayTag Channel, const FAbandonQuestEvent& Event)
{
    const FGameplayTag QuestTag = Event.GetQuestTag();
    if (!QuestTag.IsValid()) return;
    SetQuestDeactivated(QuestTag, EDeactivationSource::External, false);
}

void UQuestManagerSubsystem::ActivateNodeByTag(FName NodeTagName, FGameplayTag IncomingOutcomeTag)
{
    TObjectPtr<UQuestNodeBase>* InstancePtr = LoadedNodeInstances.Find(NodeTagName);
    if (!InstancePtr || !*InstancePtr)
    {
        UE_LOG(LogSimpleQuest, Warning,
            TEXT("UQuestManagerSubsystem::ActivateNodeByTag : no instance found for tag name '%s'"),
            *NodeTagName.ToString());
        return;
    }

    const FGameplayTag NodeTag = UGameplayTagsManager::Get().RequestGameplayTag(NodeTagName, false);

    // Diamond convergence guard — refuses activation if the node is already running, waiting for a giver, or explicitly blocked.
    // Completed is intentionally excluded so loops and repeatable quests can re-enter.
    if (NodeTag.IsValid() && WorldState)
    {
        if (WorldState->HasFact(MakeQuestStateFact(NodeTag, UQuestStateTagUtils::Leaf_Active))       ||
            WorldState->HasFact(MakeQuestStateFact(NodeTag, UQuestStateTagUtils::Leaf_PendingGiver)) ||
            WorldState->HasFact(MakeQuestStateFact(NodeTag, UQuestStateTagUtils::Leaf_Blocked)))
        {
            // Already-active Quest receiving a late outcome: deliver the outcome without re-activating. This handles graphs
            // where a Quest node is activated before its incoming outcome arrives.
            if (IncomingOutcomeTag.IsValid()
                && WorldState->HasFact(MakeQuestStateFact(NodeTag, UQuestStateTagUtils::Leaf_Active)))
            {
                if (UQuest* QuestNode = Cast<UQuest>(*InstancePtr))
                {
                    UE_LOG(LogSimpleQuest, Log, TEXT("ActivateNodeByTag: delivering late outcome '%s' to already-active quest '%s'"),
                        *IncomingOutcomeTag.ToString(), *NodeTagName.ToString());

                    const FName EntryFactName = UQuestStateTagUtils::MakeEntryOutcomeFact(NodeTagName, IncomingOutcomeTag);
                    if (!EntryFactName.IsNone())
                    {
                        WorldState->AddFact(UGameplayTagsManager::Get().RequestGameplayTag(EntryFactName, false));
                    }

                    if (const FQuestOutcomeNodeList* OutcomeEntries = QuestNode->GetEntryStepTagsByOutcome().Find(IncomingOutcomeTag))
                    {
                        for (const FName& StepTag : OutcomeEntries->NodeTags)
                        {
                            ActivateNodeByTag(StepTag);
                        }
                    }
                }
            }
            return;
        }
        // Clear Deactivated if present. A deactivated node is allowed to re-enter via its Activate input.
        WorldState->RemoveFact(MakeQuestStateFact(NodeTag, UQuestStateTagUtils::Leaf_Deactivated));
    }

    if (NodeTag.IsValid() && RegisteredGiverQuestTags.Contains(NodeTag))
    {
        (*InstancePtr)->bWasGiverGated = true;
        SetQuestPendingGiver(NodeTag);
        if (QuestSignalSubsystem)
        {
            QuestSignalSubsystem->PublishMessage(NodeTag, FQuestEnabledEvent(NodeTag, true));
        }
        return;
    }

    (*InstancePtr)->Activate(NodeTag);

    if (UQuest* QuestNode = Cast<UQuest>(*InstancePtr))
    {
        // Write entry outcome fact for prerequisite expressions within the inner graph
        if (IncomingOutcomeTag.IsValid() && WorldState)
        {
            const FName EntryFactName = UQuestStateTagUtils::MakeEntryOutcomeFact(NodeTagName, IncomingOutcomeTag);
            if (!EntryFactName.IsNone())
            {
                UE_LOG(LogSimpleQuest, Verbose, TEXT("ActivateNodeByTag: setting entry outcome fact '%s' for quest '%s'"),
                    *EntryFactName.ToString(), *NodeTagName.ToString());
                WorldState->AddFact(UGameplayTagsManager::Get().RequestGameplayTag(EntryFactName, false));
            }
        }
        
        // Always activate the "Any Outcome" entry paths
        for (const FName& StepTag : QuestNode->GetEntryStepTags())
        {
            ActivateNodeByTag(StepTag);
        }

        // If entered via a specific outcome, also activate that outcome's entry paths
        if (IncomingOutcomeTag.IsValid())
        {
            if (const FQuestOutcomeNodeList* OutcomeEntries = QuestNode->GetEntryStepTagsByOutcome().Find(IncomingOutcomeTag))
            {
                for (const FName& StepTag : OutcomeEntries->NodeTags)
                {
                    ActivateNodeByTag(StepTag);
                }
            }
        }
    }
}

void UQuestManagerSubsystem::ChainToNextNodes(UQuestNodeBase* Node, FGameplayTag OutcomeTag)
{
    if (!Node) return;
    if (Node->GetQuestTag().IsValid())
    {
        SetQuestResolved(Node->GetQuestTag(), OutcomeTag);
        if (QuestSignalSubsystem)
        {
            if (FDelegateHandle* Handle = ActiveStepTriggerHandles.Find(Node->GetQuestTag()))
            {
                QuestSignalSubsystem->UnsubscribeMessage(Node->GetQuestTag(), *Handle);
                ActiveStepTriggerHandles.Remove(Node->GetQuestTag());
            }
        }
    }

    PublishQuestEndedEvent(Node->GetQuestTag(), OutcomeTag);

    if (const TArray<FName>* OutcomeNodes = Node->GetNextNodesForOutcome(OutcomeTag))
    {
        for (const FName& Tag : *OutcomeNodes) ActivateNodeByTag(Tag, OutcomeTag);
    }
    for (const FName& Tag : Node->GetNextNodesOnAnyOutcome())
    {
        ActivateNodeByTag(Tag, OutcomeTag);
    }
}

void UQuestManagerSubsystem::SetQuestDeactivated(FGameplayTag QuestTag, EDeactivationSource Source, bool bWriteBlocked)
{
    if (!QuestTag.IsValid() || !WorldState) return;

    // Completed nodes cannot be deactivated — they are already resolved.
    if (WorldState->HasFact(MakeQuestStateFact(QuestTag, UQuestStateTagUtils::Leaf_Completed))) return;

    const FName TagName = QuestTag.GetTagName();

    // PendingGiver cleanup — remove the gate so no stale giver activation can fire later.
    if (WorldState->HasFact(MakeQuestStateFact(QuestTag, UQuestStateTagUtils::Leaf_PendingGiver)))
    {
        RegisteredGiverQuestTags.Remove(QuestTag);
        ClearQuestPendingGiver(QuestTag);
    }

    // Active node cleanup — call DeactivateInternal to tear down objectives and handle prereq subscribers
    if (WorldState->HasFact(MakeQuestStateFact(QuestTag, UQuestStateTagUtils::Leaf_Active)))
    {
        if (TObjectPtr<UQuestNodeBase>* NodePtr = LoadedNodeInstances.Find(TagName))
        {
            (*NodePtr)->DeactivateInternal(QuestTag);
        }
        WorldState->RemoveFact(MakeQuestStateFact(QuestTag, UQuestStateTagUtils::Leaf_Active));

        // Remove the objective trigger subscription for this step node.
        if (FDelegateHandle* Handle = ActiveStepTriggerHandles.Find(QuestTag))
        {
            if (QuestSignalSubsystem) QuestSignalSubsystem->UnsubscribeMessage(QuestTag, *Handle);
            ActiveStepTriggerHandles.Remove(QuestTag);
        }
            
        // Cancel any deferred completion if the step is deactivated while waiting for prerequisite completion
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

    WorldState->AddFact(MakeQuestStateFact(QuestTag, UQuestStateTagUtils::Leaf_Deactivated));
    if (bWriteBlocked) WorldState->AddFact(MakeQuestStateFact(QuestTag, UQuestStateTagUtils::Leaf_Blocked));

    // Publish on the node tag channel — triggers HandleNodeDeactivatedEvent (chaining) and any giver/watcher subscribers.
    if (QuestSignalSubsystem) QuestSignalSubsystem->PublishMessage(QuestTag, FQuestDeactivatedEvent(QuestTag, Source));
}

void UQuestManagerSubsystem::HandleNodeDeactivatedEvent(FGameplayTag Channel, const FQuestDeactivatedEvent& Event)
{
    const FName TagName = Channel.GetTagName();
    TObjectPtr<UQuestNodeBase>* NodePtr = LoadedNodeInstances.Find(TagName);
    if (!NodePtr) return;

    UQuestNodeBase* Node = *NodePtr;

    // Activate downstream nodes wired to the Deactivated output → Activate input.
    for (const FName& Tag : Node->GetNextNodesOnDeactivation())
    {
        ActivateNodeByTag(Tag);
    }
    
    // Cascade deactivation to nodes wired to the Deactivated output → Deactivate input.
    for (const FName& Tag : Node->GetNextNodesToDeactivateOnDeactivation())
    {
        const FGameplayTag TargetTag = UGameplayTagsManager::Get().RequestGameplayTag(Tag, false);
        if (TargetTag.IsValid()) SetQuestDeactivated(TargetTag, Event.Source, false);
    }
}

void UQuestManagerSubsystem::PublishQuestEndedEvent(FGameplayTag QuestTag, FGameplayTag OutcomeTag) const
{
    if (QuestSignalSubsystem && QuestTag.IsValid())
    {
        QuestSignalSubsystem->PublishMessage(QuestTag, FQuestEndedEvent(QuestTag, OutcomeTag));
    }
}

void UQuestManagerSubsystem::HandleGiveQuestEvent(FGameplayTag Channel, const FQuestGivenEvent& Event)
{
    const FGameplayTag QuestTag = Event.GetQuestTag();
    if (!QuestTag.IsValid()) return;
    RegisteredGiverQuestTags.Remove(QuestTag);
    ClearQuestPendingGiver(QuestTag);
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

void UQuestManagerSubsystem::CheckClassObjectives(FGameplayTag Channel, const FQuestObjectiveTriggered& Event)
{
    if (!Event.TriggeredActor || !QuestSignalSubsystem) return;

    // Check every class-filtered step to see if this kill is relevant
    for (const auto& Pair : ClassFilteredSteps)
    {
        if (Event.TriggeredActor->IsA(Pair.Value))
        {
            // Re-publish on the step's specific channel — existing CheckQuestObjectives handles the rest
            QuestSignalSubsystem->PublishMessage(Pair.Key, Event);
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
}

void UQuestManagerSubsystem::OnDeferredCompletionPrereqAdded(FGameplayTag Channel, const FWorldStateFactAddedEvent& Event)
{
    // Check all deferred steps — the fact that changed could satisfy any of them
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

    if (WorldState && WorldState->HasFact(MakeQuestStateFact(QuestTag, UQuestStateTagUtils::Leaf_Active)))
    {
        UE_LOG(LogSimpleQuest, Warning,
            TEXT("UQuestManagerSubsystem::HandleGiverRegisteredEvent : giver for '%s' came online after the quest already activated — gate was missed. Save the giver Blueprint to fix this for streaming scenarios."),
            *QuestTag.ToString());
    }
}

void UQuestManagerSubsystem::HandleNodeDeactivationRequest(FGameplayTag Channel, const FQuestDeactivateRequestEvent& Event)
{
    FGameplayTag EventTag = Event.GetQuestTag();
    if (EventTag.IsValid()) SetQuestDeactivated(EventTag, EDeactivationSource::Internal, Event.bWriteBlocked);
}

int32 UQuestManagerSubsystem::GetQuestCompletionCount(const FGameplayTag QuestTag) const
{
    const int32* Count = QuestCompletionCounts.Find(QuestTag);
    return Count ? *Count : 0;
}

FGameplayTag UQuestManagerSubsystem::MakeQuestStateFact(FGameplayTag QuestTag, const FString& Leaf)
{
    const FName FactName = UQuestStateTagUtils::MakeStateFact(QuestTag, Leaf);
    return UGameplayTagsManager::Get().RequestGameplayTag(FactName, false);
}

void UQuestManagerSubsystem::SetQuestActive(FGameplayTag QuestTag)
{
    if (WorldState && QuestTag.IsValid())
        WorldState->AddFact(MakeQuestStateFact(QuestTag, UQuestStateTagUtils::Leaf_Active));
}

void UQuestManagerSubsystem::SetQuestResolved(FGameplayTag QuestTag, FGameplayTag OutcomeTag)
{
    if (!WorldState || !QuestTag.IsValid()) return;
    WorldState->RemoveFact(MakeQuestStateFact(QuestTag, UQuestStateTagUtils::Leaf_Active));
    WorldState->RemoveFact(MakeQuestStateFact(QuestTag, UQuestStateTagUtils::Leaf_PendingGiver));
    WorldState->AddFact(MakeQuestStateFact(QuestTag, UQuestStateTagUtils::Leaf_Completed));
    QuestCompletionCounts.FindOrAdd(QuestTag)++;
    if (OutcomeTag.IsValid())
    {
        WorldState->AddFact(UGameplayTagsManager::Get().RequestGameplayTag(UQuestStateTagUtils::MakeNodeOutcomeFact(QuestTag.GetTagName(), OutcomeTag), false));
    }
}

void UQuestManagerSubsystem::SetQuestPendingGiver(FGameplayTag QuestTag)
{
    if (WorldState && QuestTag.IsValid())
    {
        WorldState->AddFact(MakeQuestStateFact(QuestTag, UQuestStateTagUtils::Leaf_PendingGiver));
    }
}

void UQuestManagerSubsystem::ClearQuestPendingGiver(FGameplayTag QuestTag)
{
    if (WorldState && QuestTag.IsValid())
    {
        WorldState->RemoveFact(MakeQuestStateFact(QuestTag, UQuestStateTagUtils::Leaf_PendingGiver));
    }
}
