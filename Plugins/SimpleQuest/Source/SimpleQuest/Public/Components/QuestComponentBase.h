// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "QuestComponentBase.generated.h"


struct FGameplayTag;
class USignalSubsystem;
class UQuestManagerSubsystem;

UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class SIMPLEQUEST_API UQuestComponentBase : public UActorComponent
{
	GENERATED_BODY()

public:	
	UQuestComponentBase();

	/**
	 * Applies tag renames to designer-configured tag containers that the editor-side reflection sweep can't address —
	 * specifically, TMap or TArray fields whose elements use FGameplayTag in ways that need per-class semantics (TMap
	 * with FGameplayTag keys requires remove-then-readd because TMap doesn't permit key mutation). For the common case
	 * of plain FGameplayTag and FGameplayTagContainer UPROPERTYs, the reflection sweep in
	 * FSimpleQuestEditorUtilities::ApplyTagRenamesToObject handles them generically and no override is needed. Returns
	 * the number of individual tag swaps performed by the specialty handler.
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
