// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT


#include "Components/QuestPlayerComponent.h"
#include "SimpleQuestLog.h"


// Sets default values for this component's properties
UQuestPlayerComponent::UQuestPlayerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

}

void UQuestPlayerComponent::BeginPlay()
{
	Super::BeginPlay();
}


