// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Quests/Types/QuestObjectiveContext.h"
#include "QuestManagerSubsystem.generated.h"

struct FWorldStateFactAddedEvent;
struct FQuestDeactivateRequestEvent;
struct FQuestGiverRegisteredEvent;
struct FQuestGivenEvent;
struct FQuestActivationRequestEvent;
struct FAbandonQuestEvent;
struct FQuestDeactivatedEvent;
struct FQuestEventContext;
struct FInstancedStruct;
struct FQuestText;
struct FGameplayTag;
struct FQuestStartedEvent;
struct FQuestEnabledEvent;
struct FQuestObjectiveTriggered;
enum class EDeactivationSource : uint8;
class UQuestStep;
class USignalSubsystem;
class UWorldStateSubsystem;
class IQuestTargetInterface;
class UQuestGiverInterface;
class UQuestReward;
class UQuestlineGraph;
class UQuestNodeBase;



/**
 * This class manages quests and quest givers, loading and unloading quests as needed. It handles the registration of actors
 * who have a quest giver component when they begin play. 
 */
UCLASS(Abstract, Blueprintable, config = Game)
class SIMPLEQUEST_API UQuestManagerSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

	friend class USimpleQuestBlueprintLibrary;
	
protected:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	
	void CheckQuestObjectives(FGameplayTag Channel, const FInstancedStruct& RawEvent);

	int32 GetQuestCompletionCount(FGameplayTag QuestTag) const;
	
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
	void StartInitialQuests();
	
	/** Questline graph assets to activate when the game launches. Prefer this over InitialQuests for graphs compiled with the current compiler. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="QuestMap")
	TArray<TSoftObjectPtr<UQuestlineGraph>> InitialQuestlines;

	/** Node instances from all loaded questline graph assets, keyed by tag. Populated by ActivateQuestlineGraph. */
	UPROPERTY()
	TMap<FName, TObjectPtr<UQuestNodeBase>> LoadedNodeInstances;
	
	/** Registers all compiled node instances from the graph into LoadedNodeInstances and activates its entry nodes. */
	virtual void ActivateQuestlineGraph(UQuestlineGraph* Graph);

	/**
	 * Looks up the instance for NodeTag in LoadedNodeInstances and activates it. IncomingOutcomeTag carries the outcome from
	 * the parent node for per-outcome entry routing in Quest nodes. IncomingSourceTag identifies the specific parent source
	 * whose outcome fired. Quest entry routing filters source-qualified entries against this tag so only the matching spec's
	 * entry step fires, enabling per-source routing on duplicate outcomes.
	 */
	virtual void ActivateNodeByTag(FName NodeTagName, FGameplayTag IncomingOutcomeTag = FGameplayTag(), FName IncomingSourceTag = NAME_None);

	/** Chains to next nodes after a node completes, using tag-based routing from NextNodesByOutcome / NextNodesOnAnyOutcome. */
	virtual void ChainToNextNodes(UQuestNodeBase* CompletedNode, FGameplayTag OutcomeTag);

	void PublishQuestEndedEvent(const UQuestNodeBase* Node, FGameplayTag OutcomeTag) const;

	UPROPERTY()
	TObjectPtr<USignalSubsystem> QuestSignalSubsystem;
	UPROPERTY()
	TObjectPtr<UWorldStateSubsystem> WorldState;
	
	FDelegateHandle GivenDelegateHandle;
	FDelegateHandle ObjectiveTriggeredDelegateHandle;
	FDelegateHandle AbandonDelegateHandle;

	// Append-only record of how many times each quest node has been resolved. Never decremented. Used for stats, debugging, and save data.
	TMap<FGameplayTag, int32> QuestCompletionCounts;

private:
	/**
	 * Assembles a fully populated FQuestEventContext from a node instance.
	 * Stage 1: copies NodeInfo from the node.
	 * Stage 2: copies InObjectiveData (non-empty only for completion events).
	 * Stage 3: broadcasts OnAssembleEventContext for game code to fill GameData.
	 */
	FQuestEventContext AssembleEventContext(const UQuestNodeBase* Node, const FQuestObjectiveContext& InCompletionData) const;
	
	UFUNCTION()
	void HandleOnNodeCompleted(UQuestNodeBase* Node, FGameplayTag OutcomeTag);
	UFUNCTION()
	void HandleOnNodeProgress(UQuestStep* Step, FQuestObjectiveContext ProgressData);
	UFUNCTION()
	void HandleOnNodeActivated(UQuestNodeBase* Node, FGameplayTag InContextualTag);
	UFUNCTION()
	void HandleOnNodeForwardActivated(UQuestNodeBase* Node);

	void HandleAbandonQuestEvent(FGameplayTag Channel, const FAbandonQuestEvent& Event);
	void HandleGiveQuestEvent(FGameplayTag Channel, const FQuestGivenEvent& Event);
	void HandleGiverRegisteredEvent(FGameplayTag Channel, const FQuestGiverRegisteredEvent& Event);
	void HandleNodeDeactivationRequest(FGameplayTag Channel, const FQuestDeactivateRequestEvent& Event);
	void HandleNodeDeactivatedEvent(FGameplayTag Channel, const FQuestDeactivatedEvent& Event);
	void HandleActivationRequest(FGameplayTag Channel, const FQuestActivationRequestEvent& Event);

	static FGameplayTag MakeQuestStateFact(FGameplayTag QuestTag, const FString& Leaf);
	void SetQuestActive(FGameplayTag QuestTag);
	void SetQuestResolved(FGameplayTag QuestTag, FGameplayTag OutcomeTag);
	void SetQuestPendingGiver(FGameplayTag QuestTag);
	void ClearQuestPendingGiver(FGameplayTag QuestTag);

	/**
	 * Tears down an active or pending-giver node: cancels objectives, clears WorldState facts, writes Deactivated, then
	 * publishes FQuestDeactivatedEvent on the node tag channel so subscribers (givers, watchers, and this subsystem's own
	 * HandleNodeDeactivatedEvent) can react. No-op on Completed nodes.
	 */
	void SetQuestDeactivated(FGameplayTag QuestTag, EDeactivationSource Source);

	void RegisterGiversFromAssetRegistry();	

	/**
	 * Quest tags for which at least one QuestGiverComponent Blueprint exists in the project. Populated once at Initialize
	 * from the asset registry. A node whose tag is in this set waits for a give event rather than activating immediately.
	 */
	TSet<FGameplayTag> RegisteredGiverQuestTags;
	FDelegateHandle GiverRegisteredDelegateHandle;
	FDelegateHandle DeactivateEventDelegateHandle;
	FDelegateHandle ActivationRequestDelegateHandle;
	
	TMap<FGameplayTag, FDelegateHandle> ActiveStepTriggerHandles;

	TMap<FGameplayTag, FDelegateHandle> ActiveClassTriggerHandles;
	TMultiMap<FGameplayTag, UClass*> ClassFilteredSteps;
	FDelegateHandle ClassBridgeHandle;	

	void CheckClassObjectives(FGameplayTag Channel, const FInstancedStruct& RawEvent);
	
	/** Per-node FQuestDeactivatedEvent subscription handles; populated in ActivateQuestlineGraph, cleaned up in Deinitialize. */
	TMap<FGameplayTag, FDelegateHandle> DeactivationSubscriptionHandles;

	/*------------------------------------------------------------------------------------------------------------------
	 * Deferred Completion
	 *----------------------------------------------------------------------------------------------------------------*/
	
	// Key: Step tag; Value: Pending outcome tag
	TMap<FGameplayTag, FGameplayTag> DeferredCompletionOutcomes;

	// Subscription handles for deferred completion prerequisite monitoring
	TMap<FGameplayTag, TMap<FGameplayTag, FDelegateHandle>> DeferredCompletionPrereqHandles;

	void DeferChainToNextNodes(UQuestStep* Step, FGameplayTag OutcomeTag);
	void OnDeferredCompletionPrereqAdded(FGameplayTag Channel, const FWorldStateFactAddedEvent& Event);
	void TryFireDeferredCompletion(FGameplayTag StepTag);


};
