// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "QuestComponentBase.h"
#include "Components/ActorComponent.h"
#include "Events/QuestStartedEvent.h"
#include "Interfaces/QuestGiverInterface.h"
#include "Quests/Types/QuestObjectiveActivationParams.h"
#include "QuestGiverComponent.generated.h"


struct FQuestGiverRegisteredEvent;
struct FQuestDeactivatedEvent;
struct FQuestEnabledEvent;
class UQuestManagerSubsystem;


UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class SIMPLEQUEST_API UQuestGiverComponent : public UQuestComponentBase, public IQuestGiverInterface
{
	GENERATED_BODY()

public:	
	UQuestGiverComponent();

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnQuestGiverActivated, bool, bWasQuestActivated, FGameplayTag, QuestTag, bool, bIsAnyQuestEnabled);
	
	UPROPERTY(BlueprintAssignable, BlueprintCallable, Category = "Delegates")
	FOnQuestGiverActivated OnQuestGiverActivated;
	
private:
	/**
	 * Give the quest identified by QuestTag to this component's owner. Publishes FQuestGivenEvent on Tag_Channel_QuestGiven;
	 * UQuestManagerSubsystem picks it up, clears any PendingGiver state, stamps the merged params onto the target step, and
	 * routes into the normal activation pipeline (prereq / gate checks unchanged).
	 *
	 * The outgoing params are built by additively merging two sources, in this order:
	 *   1. This component's authored ActivationParams (baseline per placed instance).
	 *   2. The caller-supplied Params argument (per-call runtime data).
	 * Merge rules match UQuestStep::ActivateInternal: TargetActors / TargetClasses union, NumElementsRequired sums,
	 * and the single-valued fields (ActivationSource, CustomData, OriginTag, OriginChain) take the caller's value when
	 * set, otherwise the authored baseline. If neither source sets ActivationSource, it defaults to GetOwner() so
	 * objectives always have a "who activated me" reference.
	 *
	 * Blueprint: the Params pin is optional thanks to AutoCreateRefTerm — leave it unconnected and the authored
	 * ActivationParams (if any) carries alone. Wire a Make FQuestObjectiveActivationParams node to supply runtime data
	 * (dialogue choices, procedural targets, typed CustomData). 
	 *
	 * C++: pass an empty struct literal (or omit the argument) for the authored-only path; fill fields to add on top.
	 *
	 * @param QuestTag  Compiled quest tag identifying which step to activate. Must be a registered tag; no-op otherwise.
	 * @param Params    Optional per-call activation payload. Merged additively with the component's ActivationParams.
	 */
	UFUNCTION(BlueprintCallable, meta = (AutoCreateRefTerm = "Params", AllowPrivateAccess = "true"))
	void GiveQuestByTag(const FGameplayTag& QuestTag, const FQuestObjectiveActivationParams& Params = FQuestObjectiveActivationParams());
	
protected:
	virtual void BeginPlay() override;
	
	/**
	 * New authoritative property — read by the manifest builder and eventually the sole registration mechanism once the
	 * full tag-based rework is complete.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Quest", meta=(Categories="Quest"))
	FGameplayTagContainer QuestTagsToGive;

	/**
	 * Designer-authored activation payload published with every give. Full FQuestObjectiveActivationParams struct —
	 * placed-actor givers can pre-wire TargetActors (the specific enemies / items this giver's quest is about), counts,
	 * typed CustomData, etc. Merged additively with the step's authored defaults in UQuestStep::ActivateInternal. Empty
	 * (default-constructed) is the common case and incurs no behavior change vs. pre-Piece-C.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Quest")
	FQuestObjectiveActivationParams ActivationParams;
	
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Quest", meta=(Categories="Quest", AllowPrivateAccess=true))
	FGameplayTagContainer EnabledQuestTags;

	virtual int32 ApplyTagRenames(const TMap<FName, FName>& Renames) override;

private:

	UFUNCTION(BlueprintCallable)
	virtual void SetQuestGiverActivated(const FGameplayTag& QuestTag, bool bIsQuestActive) override;
	
	void RegisterQuestGiver();
	void OnQuestEnabledEventReceived(FGameplayTag Channel, const FQuestEnabledEvent& Event);
	void OnQuestStartedEventReceived(FGameplayTag Channel, const FQuestStartedEvent& Event);
	void OnQuestDeactivatedEventReceived(FGameplayTag Channel, const FQuestDeactivatedEvent& Event);
	virtual void GetAssetRegistryTags(FAssetRegistryTagsContext Context) const override;

public:
	UFUNCTION(BlueprintCallable)
	FGameplayTagContainer GetQuestTagsToGive() const { return QuestTagsToGive; }
	UFUNCTION(BlueprintCallable)
	bool CanGiveAnyQuests() const;
	UFUNCTION(BlueprintCallable)
	bool IsQuestEnabled(FGameplayTag QuestTag);

};
