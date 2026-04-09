// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Subsystems/QuestManagerSubsystem.h"
#include "Quests/QuestlineGraph.h"
#include "Quests/QuestNodeBase.h"
#include "Events/QuestEndedEvent.h"
#include "Events/QuestObjectiveTriggered.h"
#include "Events/QuestStartedEvent.h"
#include "Events/QuestEnabledEvent.h"
#include "Events/AbandonQuestEvent.h"
#include "Signals/SignalSubsystem.h"
#include "GameplayTagsManager.h"
#include "SimpleQuestLog.h"
#include "Events/QuestGivenEvent.h"
#include "Events/QuestGiverRegisteredEvent.h"
#include "Objectives/QuestObjective.h"
#include "Quests/Quest.h"
#include "Quests/QuestStep.h"
#include "WorldState/WorldStateSubsystem.h"
#include "Utils/QuestStateTagUtils.h"
#if WITH_EDITOR
#include "Components/QuestGiverComponent.h"
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

void UQuestManagerSubsystem::CountQuestElement_Implementation(UObject* InQuestElement)
{
    // Node-based objectives are handled via the CheckQuestObjectives signal subscription.
}

void UQuestManagerSubsystem::CheckQuestObjectives(FGameplayTag Channel, const FQuestObjectiveTriggered& QuestObjectiveEvent)
{
    TObjectPtr<UQuestNodeBase>* NodePtr = LoadedNodeInstances.Find(Channel.GetTagName());
    if (!NodePtr) return;

    if (UQuestStep* Step = Cast<UQuestStep>(*NodePtr))
    {
        if (Step->GetActiveObjective())
        {
            Step->GetActiveObjective()->TryCompleteObjective(QuestObjectiveEvent.TriggeredActor);
        }
    }
}

void UQuestManagerSubsystem::ActivateQuestlineGraph(UQuestlineGraph* Graph)
{
    if (!Graph) return;

    for (const auto& Pair : Graph->GetCompiledNodes())
    {
        if (UQuestNodeBase* Instance = Pair.Value)
        {
            Instance->ResolveQuestTag(Pair.Key);
            LoadedNodeInstances.Add(Pair.Key, Instance);
            Instance->RegisterWithGameInstance(GetGameInstance());
            Instance->OnNodeCompleted.BindDynamic(this, &UQuestManagerSubsystem::HandleOnNodeCompleted);
            Instance->OnNodeActivated.BindDynamic(this, &UQuestManagerSubsystem::HandleOnNodeActivated);
        }
    }

    for (const FName& EntryTagName : Graph->GetEntryNodeTags())
    {
        ActivateNodeByTag(EntryTagName);
    }
}

void UQuestManagerSubsystem::HandleOnNodeCompleted(UQuestNodeBase* Node, FGameplayTag OutcomeTag)
{
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
        if (Cast<UQuestStep>(Node))
        {
            FDelegateHandle Handle = QuestSignalSubsystem->SubscribeMessage<FQuestObjectiveTriggered>(Node->GetQuestTag(), this, &UQuestManagerSubsystem::CheckQuestObjectives);
            ActiveStepTriggerHandles.Add(Node->GetQuestTag(), Handle);
        }
    }
}

void UQuestManagerSubsystem::HandleAbandonQuestEvent(FGameplayTag Channel, const FAbandonQuestEvent& Event)
{
    const FGameplayTag QuestTag = Event.GetQuestTag();
    if (!QuestTag.IsValid()) return;
    const FName TagName = QuestTag.GetTagName();
    if (TObjectPtr<UQuestNodeBase>* InstancePtr = LoadedNodeInstances.Find(TagName))
    {
        UQuestNodeBase* Node = *InstancePtr;
        SetQuestResolved(QuestTag, FGameplayTag::EmptyTag);
        PublishQuestEndedEvent(QuestTag, FGameplayTag::EmptyTag);
        for (const FName& Tag : Node->GetNextNodesOnAbandon())
        {
            ActivateNodeByTag(Tag);
        }
    }
}

void UQuestManagerSubsystem::ActivateNodeByTag(FName NodeTagName)
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

    // Diamond convergence guard — prevents double-activation of a node that is already running or queued for a giver. Completed
    // is intentionally excluded so loops and repeatable quests can re-enter a previously resolved node.
    if (NodeTag.IsValid() && WorldState)
    {
        if (WorldState->HasFact(MakeQuestStateFact(NodeTag, QuestStateTagUtils::Leaf_Active))      ||
            WorldState->HasFact(MakeQuestStateFact(NodeTag, QuestStateTagUtils::Leaf_PendingGiver)))
        {
            return;
        }
    }

    for (auto Tag : RegisteredGiverQuestTags)
    {
        UE_LOG(LogSimpleQuest, Warning, TEXT("UQuestManagerSubsystem::ActivateNodeByTag : checking registered giver tag: %s"), *Tag.ToString());
    }
    
    if (NodeTag.IsValid() && RegisteredGiverQuestTags.Contains(NodeTag))
    {
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
        for (const FName& StepTag : QuestNode->GetEntryStepTags())
        {
            ActivateNodeByTag(StepTag);
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
        for (const FName& Tag : *OutcomeNodes) ActivateNodeByTag(Tag);
    }
    for (const FName& Tag : Node->GetNextNodesOnAnyOutcome())
    {
        ActivateNodeByTag(Tag);
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
    // In editor/PIE: scan in-memory CDOs directly — always reflects current compiled state,
    // no save required. Catches compile-without-save which the AR cannot see.
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
    // Packaged build: AR scan — user data baked into cooked registry at save time.
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

void UQuestManagerSubsystem::HandleGiverRegisteredEvent(FGameplayTag Channel, const FQuestGiverRegisteredEvent& Event)
{
    const FGameplayTag QuestTag = Event.GetQuestTag();
    if (!QuestTag.IsValid()) return;

    RegisteredGiverQuestTags.Add(QuestTag);
    UE_LOG(LogSimpleQuest, Verbose, TEXT("UQuestManagerSubsystem::HandleGiverRegisteredEvent : giver registered for '%s'"), *QuestTag.ToString());

    if (WorldState && WorldState->HasFact(MakeQuestStateFact(QuestTag, QuestStateTagUtils::Leaf_Active)))
    {
        UE_LOG(LogSimpleQuest, Warning,
            TEXT("UQuestManagerSubsystem::HandleGiverRegisteredEvent : giver for '%s' came online after the quest already activated — gate was missed. Save the giver Blueprint to fix this for streaming scenarios."),
            *QuestTag.ToString());
    }
}

int32 UQuestManagerSubsystem::GetQuestCompletionCount(const FGameplayTag QuestTag) const
{
    const int32* Count = QuestCompletionCounts.Find(QuestTag);
    return Count ? *Count : 0;
}

FGameplayTag UQuestManagerSubsystem::MakeQuestStateFact(FGameplayTag QuestTag, const FString& Leaf)
{
    const FName FactName = QuestStateTagUtils::MakeStateFact(QuestTag, Leaf);
    return UGameplayTagsManager::Get().RequestGameplayTag(FactName, false);
}

void UQuestManagerSubsystem::SetQuestActive(FGameplayTag QuestTag)
{
    if (WorldState && QuestTag.IsValid())
        WorldState->AddFact(MakeQuestStateFact(QuestTag, QuestStateTagUtils::Leaf_Active));
}

void UQuestManagerSubsystem::SetQuestResolved(FGameplayTag QuestTag, FGameplayTag OutcomeTag)
{
    if (!WorldState || !QuestTag.IsValid()) return;
    WorldState->RemoveFact(MakeQuestStateFact(QuestTag, QuestStateTagUtils::Leaf_Active));
    WorldState->RemoveFact(MakeQuestStateFact(QuestTag, QuestStateTagUtils::Leaf_PendingGiver));
    WorldState->AddFact(MakeQuestStateFact(QuestTag, QuestStateTagUtils::Leaf_Completed));
    QuestCompletionCounts.FindOrAdd(QuestTag)++;
    if (OutcomeTag.IsValid())
    {
        WorldState->AddFact(UGameplayTagsManager::Get().RequestGameplayTag(
            QuestStateTagUtils::MakeOutcomeFact(OutcomeTag), false));
    }
}

void UQuestManagerSubsystem::SetQuestPendingGiver(FGameplayTag QuestTag)
{
    if (WorldState && QuestTag.IsValid())
    {
        WorldState->AddFact(MakeQuestStateFact(QuestTag, QuestStateTagUtils::Leaf_PendingGiver));
    }
}

void UQuestManagerSubsystem::ClearQuestPendingGiver(FGameplayTag QuestTag)
{
    if (WorldState && QuestTag.IsValid())
    {
        WorldState->RemoveFact(MakeQuestStateFact(QuestTag, QuestStateTagUtils::Leaf_PendingGiver));
    }
}
