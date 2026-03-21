// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Interfaces/QuestEventListenerInterface.h"
#include "QuestComponentBase.generated.h"


class UQuestSignalSubsystem;
class UQuestManagerSubsystem;

UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class SIMPLEQUEST_API UQuestComponentBase : public UActorComponent, public IQuestEventListenerInterface
{
	GENERATED_BODY()

public:	
	UQuestComponentBase();

protected:
	virtual void PostInitProperties() override;
	virtual void BeginPlay() override;

	//bool CheckQuestManager();
	bool CheckQuestSignalSubsystem();

	// void RegisterForQuestClass(UClass* LoadedQuestClass);
		
	//UPROPERTY(VisibleAnywhere)
	//TObjectPtr<UQuestManagerSubsystem> QuestManager;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UQuestSignalSubsystem> QuestSignalSubsystem;	
};
