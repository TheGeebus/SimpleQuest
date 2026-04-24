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

	/**
	 * Removes the listed tags from every designer-configured tag container on this component. Called by the Stale Quest Tags
	 * panel's per-row Clear action. Each concrete component override is responsible for dirtying the owning actor after
	 * modification so the change survives a save. Returns the number of individual tag removals performed.
	 */
	virtual int32 RemoveTags(const TArray<FGameplayTag>& TagsToRemove);
	
protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USignalSubsystem> SignalSubsystem;
};
