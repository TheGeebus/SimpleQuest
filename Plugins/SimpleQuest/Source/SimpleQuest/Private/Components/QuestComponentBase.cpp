// Copyright 2026, Greg Bussell, All Rights Reserved.


#include "Components/QuestComponentBase.h"

#include "SimpleQuestLog.h"
#include "Signals/SignalSubsystem.h"


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
			SignalSubsystem = GameInstance->GetSubsystem<USignalSubsystem>();
		}
	}
}

void UQuestComponentBase::BeginPlay()
{
	Super::BeginPlay();
}

bool UQuestComponentBase::CheckQuestSignalSubsystem()
{
	if (SignalSubsystem.Get() == nullptr)
	{
		if (const UWorld* World = GetWorld())
		{
			if (const UGameInstance* GameInstance = World->GetGameInstance())
			{
				SignalSubsystem = GameInstance->GetSubsystem<USignalSubsystem>();
				if (SignalSubsystem == nullptr)
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
	return SignalSubsystem != nullptr;
}
