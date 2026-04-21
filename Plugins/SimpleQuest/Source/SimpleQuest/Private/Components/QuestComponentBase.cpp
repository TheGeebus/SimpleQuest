// Copyright 2026, Greg Bussell, All Rights Reserved.


#include "Components/QuestComponentBase.h"

#include "SimpleQuestLog.h"
#include "Signals/SignalSubsystem.h"


UQuestComponentBase::UQuestComponentBase()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UQuestComponentBase::BeginPlay()
{
	Super::BeginPlay();
	if (const UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr)
	{
		SignalSubsystem = GI->GetSubsystem<USignalSubsystem>();
	}
	ensureMsgf(SignalSubsystem, TEXT("UQuestComponentBase: SignalSubsystem unavailable on %s — is SimpleCore loaded?"), *GetOwner()->GetActorNameOrLabel());
}

int32 UQuestComponentBase::ApplyTagRenames(const TMap<FName, FName>& Renames)
{
	return 0;
}

