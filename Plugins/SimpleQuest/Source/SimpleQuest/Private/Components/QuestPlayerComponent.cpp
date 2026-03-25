// Fill out your copyright notice in the Description page of Project Settings.


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


