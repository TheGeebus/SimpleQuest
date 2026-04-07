// Copyright 2026, Greg Bussell, All Rights Reserved.


#include "Subsystems/QuestManagerSubsystem.h"
#include "Quests/QuestlineGraph.h"
#include "Quests/QuestNodeBase.h"
#include "Events/QuestEndedEvent.h"
#include "Events/QuestObjectiveTriggered.h"
#include "Events/QuestStepCompletedEvent.h"
#include "Events/QuestStepStartedEvent.h"
#include "Events/QuestStartedEvent.h"
#include "Events/QuestEnabledEvent.h"
#include "Interfaces/QuestTargetInterface.h"
#include "Signals/SignalSubsystem.h"
#include "GameplayTagsManager.h"
#include "SimpleQuestLog.h"
#include "Objectives/QuestObjective.h"
#include "Quests/Quest.h"
#include "Quests/QuestStep.h"
#include "Utils/SimpleCoreDebug.h"


void UQuestManagerSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	if (UGameInstance* GameInstance = GetGameInstance())
	{
		QuestSignalSubsystem = GameInstance->GetSubsystem<USignalSubsystem>();
		if (QuestSignalSubsystem)
		{
			ObjectiveTriggeredDelegateHandle = QuestSignalSubsystem->SubscribeTyped<FQuestObjectiveTriggered>(UQuestTargetInterface::StaticClass(), this, &UQuestManagerSubsystem::CheckQuestObjectives);
		}
	}
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
		QuestSignalSubsystem->UnsubscribeTyped<FQuestObjectiveTriggered>(UQuestTargetInterface::StaticClass(), ObjectiveTriggeredDelegateHandle);
	}
	Super::Deinitialize();
}

void UQuestManagerSubsystem::StartInitialQuests_Implementation()
{
	// Start initial questline assets compiled by visual graph
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

void UQuestManagerSubsystem::CheckQuestObjectives(const FQuestObjectiveTriggered& QuestObjectiveEvent)
{
	for (auto& Pair : LoadedNodeInstances)
	{
		if (UQuestStep* Step = Cast<UQuestStep>(Pair.Value))
		{
			if (Step->GetActiveObjective() && Step->GetActiveObjective()->IsObjectRelevant(QuestObjectiveEvent.TriggeredActor))
			{
				Step->GetActiveObjective()->TryCompleteObjective(QuestObjectiveEvent.TriggeredActor);
			}
		}
	}

}

/*
void UQuestManagerSubsystem::QueueCommsEvent(const FCommsEvent& InCommsEvent)
{
	const FCommsEvent NewEvent = InCommsEvent;
	CommsEventQueue.Add(NewEvent);
	if (GetWorld() && !GetWorld()->GetTimerManager().IsTimerActive(CommsEventTimerHandle))
	{
		StartCommsEvent();
	}
}

void UQuestManagerSubsystem::StartCommsEvent()
{
	if (CommsEventQueue.Num() <= 0) { return; }
	const FCommsEvent& CommsEvent = CommsEventQueue[0];
	USoundBase* Sound = CommsEvent.Sound.LoadSynchronous();
	float Duration = CommsEvent.ViewDuration;
	if (Sound != nullptr)
	{
		AudioComponent = UGameplayStatics::CreateSound2D(this, Sound);
		if (AudioComponent != nullptr)
		{
			AudioComponent->Play();
			if (!CommsEvent.bOverrideDuration)
			{
				Duration = Sound->GetDuration() + CommsEventSubtitleDelay;
			}
		}
		else
		{
			UE_LOG(LogSimpleQuest, Warning, TEXT("UQuestManagerSubsystem::StartCommsEvent : Quest manager could not create audio component"))
		}
	}
	else
	{
		UE_LOG(LogSimpleQuest, Warning, TEXT("UQuestManagerSubsystem::StartCommsEvent : Quest manager cannot play sound as it is null"))
	}
		
	FTimerDelegate TimerDelegate = FTimerDelegate::CreateUObject(this, &UQuestManagerSubsystem::CommsEventTimerEnd);
	GetWorld()->GetTimerManager().SetTimer(CommsEventTimerHandle,
		TimerDelegate,
		Duration,
		false);
	if (OnCommsEventStart.IsBound())
	{
		OnCommsEventStart.Broadcast(CommsEventQueue[0]);
	}
}

void UQuestManagerSubsystem::CommsEventTimerEnd()
{
	if (OnCommsEventEnd.IsBound())
	{
		OnCommsEventEnd.Broadcast();
	}
	CommsEventQueue.RemoveAt(0);
	StartCommsEvent();
}

void UQuestManagerSubsystem::UpdateQuestText(const FQuestText& InQuestText)
{
	if (OnQuestTextUpdated.IsBound())
	{
		OnQuestTextUpdated.Broadcast(InQuestText);
	}
}

void UQuestManagerSubsystem::UpdateQuestTextVisibility(bool bIsVisible, bool bUseCounter)
{
	if (OnQuestTextVisibilityUpdated.IsBound())
	{
		OnQuestTextVisibilityUpdated.Broadcast(bIsVisible, bUseCounter);
	}
}
*/

void UQuestManagerSubsystem::OnStepTargetEnabledEvent(UQuestStep* Step, UObject* TargetObject, bool bIsEnabled)
{
	if (!QuestSignalSubsystem) return;

	if (bIsEnabled)
	{
		QuestSignalSubsystem->PublishTyped(TargetObject, FQuestStepStartedEvent(Step->GetQuestTag()));
	}
	else
	{
		QuestSignalSubsystem->PublishTyped(TargetObject, FQuestStepCompletedEvent(Step->GetQuestTag(), true, false, nullptr));
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
			Instance->OnNodeCompleted.BindDynamic(this, &UQuestManagerSubsystem::HandleOnNodeCompleted);
			Instance->OnNodeActivated.BindDynamic(this, &UQuestManagerSubsystem::HandleOnNodeActivated);
			if (UQuestStep* Step = Cast<UQuestStep>(Instance))
			{
				Step->OnStepTargetEnabled.BindUObject(this, &UQuestManagerSubsystem::OnStepTargetEnabledEvent);
			}
		}
	}

	for (const FName& EntryTagName : Graph->GetEntryNodeTags())
	{
		UE_LOG(LogSimpleQuest, Warning, TEXT(""))
		ActivateNodeByTag(EntryTagName);
	}
}

void UQuestManagerSubsystem::HandleOnNodeCompleted(UQuestNodeBase* Node, bool bDidSucceed)
{
	ChainToNextNodes(Node, bDidSucceed);
}

void UQuestManagerSubsystem::HandleOnNodeActivated(UQuestNodeBase* Node, FGameplayTag InContextualTag)
{
	if (Node->GetQuestTag().IsValid())
	{
		ActiveQuestTags.AddTag(Node->GetQuestTag());
	}
	// Signal for watcher/HUD integration — typed to the node's class as broadcast channel
	// (no specific quest class in the node system; subscribers listen on UQuestNode::StaticClass())
	if (QuestSignalSubsystem)
	{
		QuestSignalSubsystem->PublishTyped(UQuestNodeBase::StaticClass(), FQuestStartedEvent(Node->GetQuestTag()));
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
	if ((*InstancePtr)->IsGiverGated())
	{
		// Don't activate yet — notify givers this node is available
		if (QuestSignalSubsystem)
		{
			QuestSignalSubsystem->PublishTyped(UQuestNodeBase::StaticClass(), FQuestEnabledEvent(NodeTag, true));
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

void UQuestManagerSubsystem::ChainToNextNodes(UQuestNodeBase* CompletedNode, bool bDidSucceed)
{
	if (!CompletedNode) return;

	if (CompletedNode->GetQuestTag().IsValid()) ActiveQuestTags.RemoveTag(CompletedNode->GetQuestTag());

	if (QuestSignalSubsystem)
	{
		QuestSignalSubsystem->PublishTyped(UQuestNodeBase::StaticClass(), FQuestEndedEvent(CompletedNode->GetQuestTag(), bDidSucceed));
	}
	
	const TSet<FName>& NextTagNames = bDidSucceed
		? CompletedNode->GetNextNodesOnSuccess()
		: CompletedNode->GetNextNodesOnFailure();

	for (const FName& TagName : NextTagNames)
	{
		ActivateNodeByTag(TagName);
	}
}

void UQuestManagerSubsystem::PublishQuestEndedEvent(FGameplayTag QuestTag, bool bDidSucceed) const
{
	if (QuestSignalSubsystem)
	{
		QuestSignalSubsystem->PublishTyped(UQuestNodeBase::StaticClass(), FQuestEndedEvent(QuestTag, bDidSucceed));
	}
}

void UQuestManagerSubsystem::GiveNodeQuest(FGameplayTag NodeTag)
{
	ActivateNodeByTag(NodeTag.GetTagName());
}

