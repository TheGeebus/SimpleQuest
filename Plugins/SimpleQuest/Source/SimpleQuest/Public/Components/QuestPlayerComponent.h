// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "QuestComponentBase.h"
#include "Components/ActorComponent.h"
#include "QuestPlayerComponent.generated.h"


class UQuestManagerSubsystem;

UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class SIMPLEQUEST_API UQuestPlayerComponent : public UQuestComponentBase
{
	GENERATED_BODY()

public:	
	UQuestPlayerComponent();

protected:
	virtual void BeginPlay() override;
	
	
};
