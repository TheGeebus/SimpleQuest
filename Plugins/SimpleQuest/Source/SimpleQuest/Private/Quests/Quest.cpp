// Copyright 2026, Greg Bussell, All Rights Reserved.


#include "Quests/Quest.h"

#include "Rewards/QuestReward.h"
#include "Objectives/QuestObjective.h"
#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "SimpleQuest"

UQuest::UQuest()
{
	UE_LOG(LogSimpleQuest, Verbose, TEXT("UQuest constructor called, creating: %s"), *GetFullName());
}

void UQuest::PostInitProperties()
{
	Super::PostInitProperties();

	LoadAutoActivatedQuestSteps();
}

void UQuest::LoadAutoActivatedQuestSteps()
{
	int32 i = 0;
	for (const auto& Step : QuestSteps)
	{
		if (Step.bActivateOnQuestLoad)
		{
			UE_LOG(LogSimpleQuest, Log, TEXT("UQuest::LoadQuestSteps : Loading quest step: %i"), i);
			StartQuestStep(i);
		}
		i++;
	}
}

bool UQuest::IsQuestStepValid(const int32 InStepID) const
{
	return InStepID >= 0 && InStepID < QuestSteps.Num();
}

UQuestObjective* UQuest::LoadQuestObjective(int32 InStepID)
{
	if (!IsQuestStepValid(InStepID))
	{
		return nullptr;
	}
	if (QuestSteps[InStepID].QuestObjective->IsValidLowLevel())
	{
		if (UQuestObjective* FoundObjective = FindLoadedQuestObjective(InStepID))
		{
			return FoundObjective;
		}
		UE_LOG(LogSimpleQuest, Verbose, TEXT("UQuest::LoadQuestObjective : Quest Objective is valid"));
		UQuestObjective* NewObjectiveCDO = QuestSteps[InStepID].QuestObjective->GetDefaultObject<UQuestObjective>();
		UQuestObjective* NewObjective = NewObject<UQuestObjective>(
			this,
			QuestSteps[InStepID].QuestObjective,
			NAME_None,
			RF_NoFlags,
			NewObjectiveCDO);
		if (NewObjective != nullptr)
		{
			/*
			if (QuestSignalSubsystem != nullptr)
			{
				NewObjective->SetQuestSignalSubsystem(QuestSignalSubsystem);
			}
			*/
			UE_LOG(LogSimpleQuest, Verbose, TEXT("UQuest::LoadQuestObjective : Quest Objective Loaded"));
			LoadedQuestObjectives.Add(InStepID, NewObjective);
			UE_LOG(LogSimpleQuest, Verbose, TEXT("UQuest::LoadQuestObjective : Quest Step Objective: %s"), *QuestSteps[InStepID].QuestObjective->GetName());
			ActivateQuestObjective(InStepID);
			return NewObjective;
		}
	}
	return nullptr;
}

UQuestObjective* UQuest::FindLoadedQuestObjective(int32 InStepID)
{
	if (TObjectPtr<UQuestObjective>* ObjectivePtr = LoadedQuestObjectives.Find(InStepID))
	{
		return *ObjectivePtr;
	}
	return nullptr;
}

void UQuest::CheckQuestTarget(UObject* InQuestTargetObject)
{
	TSet<UQuestObjective*> ObjectivesToCheck;
	for (const auto ObjectivePair : LoadedQuestObjectives)
	{
		if (!IsValid(ObjectivePair.Value)) continue;
		
		const FQuestStep& Step = QuestSteps[ObjectivePair.Key];
		// Check for completion of all prerequisite quest steps
		if (Step.PrerequisiteSteps.IsEmpty())
		{
			ObjectivesToCheck.Add(LoadedQuestObjectives[ObjectivePair.Key]);
		}
		else
		{
			bool bFoundUnfinishedPrereq = false;
			for (const auto StepID : Step.PrerequisiteSteps)
			{
				const bool* SuccessPtr = CompletedSteps.Find(StepID);
				// SuccessPtr will be null if the prerequisite StepID has not been added to the CompletedSteps map.
				// If all prerequisites must succeed, additionally check the step success value from the map.
				if (SuccessPtr == nullptr || (Step.bPrerequisitesMustSucceed && !*SuccessPtr))
				{
					UE_LOG(LogSimpleQuest, Verbose, TEXT("UQuest::CheckQuestTarget : step has prereqs that are not complete or did not succeed, step: %i"), StepID);
					bFoundUnfinishedPrereq = true;
				}
			}
			if (!bFoundUnfinishedPrereq)
			{
				ObjectivesToCheck.Add(LoadedQuestObjectives[ObjectivePair.Key]);
			}
			else if (ObjectivePair.Value->IsObjectRelevant(InQuestTargetObject))
			{
				UE_LOG(LogSimpleQuest, Verbose, TEXT("UQuest::CheckQuestTarget : prereqs not met for step %i but target is relevant, firing OnQuestStepPrereqsFail"), ObjectivePair.Key);
				OnQuestStepPrereqsFail.ExecuteIfBound(this, ObjectivePair.Key);
			}			
		}
	}
	if (!ObjectivesToCheck.IsEmpty())
	{
		for (const auto Objective : ObjectivesToCheck)
		{
			UE_LOG(LogSimpleQuest, Verbose, TEXT("UQuest::CheckQuestTarget : checking objective: %s"), *Objective->GetName());
			if (IsValid(Objective) && Objective->IsObjectRelevant(InQuestTargetObject))
			{
				Objective->TryCompleteObjective(InQuestTargetObject);
			}
		}
	}
}

void UQuest::UpdateQuestCounter(int32 InStepID, int32 NewDisplayAmount)
{
	FQuestStep* Step = &QuestSteps[InStepID];
	if (Step != nullptr)
	{
		Step->QuestWidgetText.CounterCurrentValue = NewDisplayAmount;
		UE_LOG(LogSimpleQuest, Verbose, TEXT("UQuest::UpdateQuestCounter : QuestText counter updated to: %i"), Step->QuestWidgetText.CounterCurrentValue);
		OnUpdateQuestText.ExecuteIfBound(Step->QuestWidgetText);
	}
}

void UQuest::ActivateQuestObjective(const int32 InStepID)
{
	OnQuestStepStarted.ExecuteIfBound(this, InStepID);
	UQuestObjective* Objective = FindLoadedQuestObjective(InStepID);
	FQuestStep* Step = &QuestSteps[InStepID];
	if (Objective)
	{
		if (Step)
		{
			UE_LOG(LogSimpleQuest, Verbose, TEXT("UQuest::ActivateQuestObjective : binding delegates for quest step: %i"), InStepID);
			Objective->OnEnableTarget.AddDynamic(this, &UQuest::OnObjectiveEnabledEvent);
			Objective->SetObjectiveTarget(InStepID, Step->GetTargetActors(), Step->TargetClass, Step->NumberOfElements, Step->QuestWidgetText.bUseCounter);
			Objective->OnQuestObjectiveComplete.AddDynamic(this, &UQuest::StepFinished);
			if (Step->QuestWidgetText.bUseCounter)
			{
				Objective->OnTargetTriggered.BindUObject(this, &UQuest::UpdateQuestCounter);
			}
			
			if (Step->bUpdateQuestWidget)
			{
				UE_LOG(LogSimpleQuest, Verbose, TEXT("UQuest::ActivateQuestObjective : Attempting to show quest widget"));
				Step->QuestWidgetText.CounterMaxValue = Step->NumberOfElements;
				OnUpdateQuestText.ExecuteIfBound(Step->QuestWidgetText);
				OnSetQuestTextVisibility.ExecuteIfBound(true, Step->QuestWidgetText.bUseCounter);
			}

			if (Step->bUseStartEvent)
			{
				OnQueueCommsEvent.ExecuteIfBound(Step->StartEvent);
			}
		}
		else
		{
			UE_LOG(LogSimpleQuest, Error, TEXT("UQuest::ActivateQuestObjective : Quest Step is invalid"));
		}
	}
	else
	{
		UE_LOG(LogSimpleQuest, Error, TEXT("UQuest::ActivateQuestObjective : Quest Objective is invalid"));
	}
}

void UQuest::StartQuestStep(int32 InQuestStepID)
{
	UE_LOG(LogSimpleQuest, Log, TEXT("UQuest::StartQuestStep : QuestStep Started, step index: %i"), InQuestStepID);
	if (InQuestStepID < 0)
	{
		UE_LOG(LogSimpleQuest, Warning, TEXT("UQuest::StartQuestStep : Attempted to start quest step -1. Must change this default value on each step in quest: %s"), *this->GetClass()->GetName());
		return;
	}
	if (QuestSteps.Num() - 1 < InQuestStepID)
	{
		UE_LOG(LogSimpleQuest, Warning, TEXT("UQuest::StartQuestStep : QuestStepID out of bounds, InQuestStepID: %i"), InQuestStepID);
		return;
	}
	
	// Debugging, list currently active quest steps 
	for (const auto Item : CurrentActiveSteps)
	{
		UE_LOG(LogSimpleQuest, Verbose, TEXT("UQuest::StartQuestStep : Current Active Steps: %i"), Item);
	}

	if (!CurrentActiveSteps.Contains(InQuestStepID))
	{
		if (FQuestStep* Step = &QuestSteps[InQuestStepID])
		{
			// Load the QuestObjective object, binding the necessary events.
			if (LoadQuestObjective(InQuestStepID))
			{
				CurrentActiveSteps.Add(InQuestStepID);
			}
			// Steps can be completed early but should only be unloaded after they have been started normally.
			if (Step->bStepCompleted)
			{
				StepFinished(InQuestStepID, true);
			}
		}
	}
}

void UQuest::StepFinished_Implementation(int32 CompletedStepID, bool bDidSucceed)
{
	// Get a reference to the completed step
	FQuestStep* Step = &QuestSteps[CompletedStepID];

	// Flag as completed
	Step->bStepCompleted = true;
	// Do not consider this step active in future searches
	if (CurrentActiveSteps.Contains(CompletedStepID))
	{
		CurrentActiveSteps.Remove(CompletedStepID);
	}
	// Potential prerequisite is finished
	CompletedSteps.Add(CompletedStepID, bDidSucceed);
	// Queue a CommsEvent if one is scheduled at the end of this quest step	
	if (Step->bUseEndEvent)
	{
		OnQueueCommsEvent.ExecuteIfBound(Step->EndEvent);
	}
	// Remove the reference to the loaded quest objective so that it may be garbage collected
	LoadedQuestObjectives.Remove(CompletedStepID);

	// Get the quest reward
	UQuestReward* QuestReward = nullptr;
	if (UClass* RewardClass = Step->QuestReward.LoadSynchronous())
	{
		QuestReward = NewObject<UQuestReward>(this, RewardClass);
	}
	// If the step ends the quest, do so, notifying the quest manager 
	if (Step->bCompletesQuest)
	{
		OnQuestStepComplete.ExecuteIfBound(this, CompletedStepID, bDidSucceed, true, QuestReward);
		FinishQuest(bDidSucceed);
		return;
	}
	
	// Start any next quest steps that have been explicitly selected 
	if (!Step->NextSteps.IsEmpty())
	{
		for (const auto StepID : Step->NextSteps)
		{
			OnQuestStepComplete.ExecuteIfBound(this, CompletedStepID, bDidSucceed, false, QuestReward);
			StartQuestStep(StepID);
		}
		return;
	}
	// No NextSteps are specified, so Auto-progress to the next quest step in the QuestSteps array if one exists  
	if (IsQuestStepValid(CompletedStepID + 1))
	{
		if (Step->bAutoProgress) // auto-progression may be disabled for each step
		{
			OnQuestStepComplete.ExecuteIfBound(this, CompletedStepID, bDidSucceed, false, QuestReward);
			StartQuestStep(CompletedStepID + 1);
			return;
		}
	}
	// Handle quest auto-completion, which can be disabled. Only called if NextSteps contains no entries and
	// there is no step after this one in the QuestSteps array 
	if (bQuestAutoComplete)
	{
		OnQuestStepComplete.ExecuteIfBound(this, CompletedStepID, bDidSucceed, true, QuestReward);
		FinishQuest(bDidSucceed);
	}
}

void UQuest::FinishQuest(bool bDidSucceed)
{
	OnQuestComplete.ExecuteIfBound(this, bDidSucceed);
	if (bCompletesQuestline)
	{
		OnQuestlineComplete.ExecuteIfBound(this, bDidSucceed);
	}
}

void UQuest::OnObjectiveEnabledEvent(UObject* InTargetObject, int32 InStepID, bool bNewIsEnabled)
{
	UE_LOG(LogSimpleQuest, Verbose, TEXT("UQuest::OnObjectiveEnabledEvent : attempting to enable target: %s"), *InTargetObject->GetFName().ToString());
	OnObjectiveEnabled.ExecuteIfBound(this, InTargetObject, InStepID, bNewIsEnabled);
}

/*
#if WITH_EDITOR

EDataValidationResult UQuest::IsDataValid(FDataValidationContext& Context) const
{
	
	EDataValidationResult Result = Super::IsDataValid(Context);
	
	IAssetRegistry& AssetRegistry = FAssetRegistryModule::GetRegistry();
	TArray<FAssetData> QuestAssets;
	FARFilter Filter;
	Filter.ClassPaths.Add(UQuest::StaticClass()->GetClassPathName());
	Filter.bRecursiveClasses = true;
	AssetRegistry.GetAssets(Filter, QuestAssets);

	const FName ThisID = GetQuestID();
	for (const FAssetData& Asset : QuestAssets)
	{
		if (Asset.GetAsset() == this) continue;
		if (const UQuest* Other = Cast<UQuest>(Asset.GetAsset()))
		{
			if (Other->GetQuestID() == ThisID)
			{
				Context.AddError(FText::Format(
					LOCTEXT("DuplicateQuestID", "QuestID '{0}' is already used by {1}."),
					FText::FromName(ThisID),
					FText::FromString(Other->GetPathName())
				));
				Result = EDataValidationResult::Invalid;
			}
		}
	}
	return Result;
}
#endif
*/

#undef LOCTEXT_NAMESPACE
