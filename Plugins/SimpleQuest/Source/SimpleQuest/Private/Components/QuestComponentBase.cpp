// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/QuestComponentBase.h"

#include "SimpleQuestLog.h"
#include "Subsystems/QuestSignalSubsystem.h"


UQuestComponentBase::UQuestComponentBase()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UQuestComponentBase::PostInitProperties()
{
	Super::PostInitProperties();
	if (const UWorld* World = GetWorld())
	{
		if (const UGameInstance* GameInstance = World->GetGameInstance())
		{
			QuestSignalSubsystem = GameInstance->GetSubsystem<UQuestSignalSubsystem>();
		}
	}
}

void UQuestComponentBase::BeginPlay()
{
	Super::BeginPlay();
}

bool UQuestComponentBase::CheckQuestSignalSubsystem()
{
	if (QuestSignalSubsystem.Get() == nullptr)
	{
		if (const UWorld* World = GetWorld())
		{
			if (const UGameInstance* GameInstance = World->GetGameInstance())
			{
				QuestSignalSubsystem = GameInstance->GetSubsystem<UQuestSignalSubsystem>();
				if (QuestSignalSubsystem == nullptr)
				{
					UE_LOG(LogSimpleQuest, Error, TEXT("UQuestComponentBase::CheckQuestSignalSubsystem : QuestSignalSubsystem was null"));
				}
			}
			else
			{
				UE_LOG(LogSimpleQuest, Error, TEXT("UQuestComponentBase::CheckQuestSignalSubsystem : GameInstance was null"));
			}
		}
		else
		{
			UE_LOG(LogSimpleQuest, Error, TEXT("UQuestComponentBase::CheckQuestSignalSubsystem : World is null"));
		}
	}
	return QuestSignalSubsystem != nullptr;
}
