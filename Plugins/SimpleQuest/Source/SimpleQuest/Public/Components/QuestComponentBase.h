// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Interfaces/QuestEventListenerInterface.h"
#include "QuestComponentBase.generated.h"


class USignalSubsystem;
class UQuestManagerSubsystem;

UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class SIMPLEQUEST_API UQuestComponentBase : public UActorComponent, public IQuestEventListenerInterface
{
	GENERATED_BODY()

public:	
	UQuestComponentBase();

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USignalSubsystem> SignalSubsystem;
};
