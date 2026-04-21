// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Interfaces/QuestEventListenerInterface.h"
#include "QuestComponentBase.generated.h"


struct FGameplayTag;
class USignalSubsystem;
class UQuestManagerSubsystem;

UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class SIMPLEQUEST_API UQuestComponentBase : public UActorComponent, public IQuestEventListenerInterface
{
	GENERATED_BODY()

public:	
	UQuestComponentBase();

	/**
	 * Applies tag renames to designer-configured tag containers. Called by the editor rename propagation system. Returns the
	 * number of individual tag swaps performed.
	 */
	virtual int32 ApplyTagRenames(const TMap<FName, FName>& Renames);
	
protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USignalSubsystem> SignalSubsystem;
};
