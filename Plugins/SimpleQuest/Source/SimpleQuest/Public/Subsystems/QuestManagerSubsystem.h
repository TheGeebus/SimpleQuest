// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Quests/Types/PrerequisiteExpression.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Quests/Types/QuestObjectiveContext.h"
#include "Quests/Types/QuestResolutionRecord.h"
#include "Quests/Types/PrereqLeafSubscription.h"
#include "QuestManagerSubsystem.generated.h"


struct FQuestResolutionRecordedEvent;
struct FQuestEntryRecordedEvent;
struct FWorldStateFactRemovedEvent;
struct FQuestActivationBlocker;
struct FQuestPrereqStatus;
struct FWorldStateFactAddedEvent;
struct FQuestDeactivateRequestEvent;
struct FQuestGiverRegisteredEvent;
struct FQuestGivenEvent;
struct FQuestActivationRequestEvent;
struct FQuestDeactivatedEvent;
struct FQuestlineStartRequestEvent;
struct FQuestResolveRequestEvent;
struct FQuestClearBlockRequestEvent;
struct FQuestBlockRequestEvent;
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
class UQuestStateSubsystem;
class UQuestTargetInterface;
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
	 * the parent node for per-path entry routing in Quest nodes. IncomingSourceTag identifies the specific parent source
	 * whose outcome fired. Quest entry routing filters source-qualified entries against this tag so only the matching spec's
	 * entry step fires, enabling per-source routing on duplicate paths.
	 */
	virtual void ActivateNodeByTag(FName NodeTagName, FGameplayTag IncomingOutcomeTag = FGameplayTag(), FName IncomingSourceTag = NAME_None);

	/** Chains to next nodes after a node completes, using tag-based routing from NextNodesByPath / NextNodesOnAnyOutcome. */
	virtual void ChainToNextNodes(UQuestNodeBase* CompletedNode, FGameplayTag OutcomeTag, FName PathIdentity);

	void PublishQuestEndedEvent(const UQuestNodeBase* Node, FGameplayTag OutcomeTag, EQuestResolutionSource Source) const;

	UPROPERTY()
	TObjectPtr<USignalSubsystem> QuestSignalSubsystem;
	UPROPERTY()
	TObjectPtr<UWorldStateSubsystem> WorldState;
	UPROPERTY()
	TObjectPtr<UQuestStateSubsystem> QuestStateSubsystem;

	/**
	 * Rich-record registry paired with WorldState's QuestState.<X>.Completed fact. Written atomically alongside
	 * WorldState in SetQuestResolved; read by catch-up paths on UQuestEventSubscription and UQuestWatcherComponent.
	 * Holds the current session's resolution record per quest: outcome, timestamp, running count.
	 */
	UPROPERTY()
	TMap<FGameplayTag, FQuestResolutionRecord> QuestResolutions;

private:
	/**
	 * Assembles a fully populated FQuestEventContext from a node instance.
	 * Stage 1: copies NodeInfo from the node.
	 * Stage 2: copies InObjectiveData (non-empty only for completion events).
	 * Stage 3: broadcasts OnAssembleEventContext for game code to fill GameData.
	 */
	FQuestEventContext AssembleEventContext(const UQuestNodeBase* Node, const FQuestObjectiveContext& InCompletionContext) const;
	
	UFUNCTION()
	void HandleOnNodeCompleted(UQuestNodeBase* Node, FGameplayTag OutcomeTag, FName PathIdentity);
	UFUNCTION()
	void HandleOnNodeProgress(UQuestStep* Step, FQuestObjectiveContext ProgressData);
	UFUNCTION()
	void HandleOnNodeStarted(UQuestNodeBase* Node, FGameplayTag InContextualTag);
	UFUNCTION()
	void HandleOnNodeForwardActivated(UQuestNodeBase* Node);

	void HandleGiveQuestEvent(FGameplayTag Channel, const FQuestGivenEvent& Event);
	void HandleGiverRegisteredEvent(FGameplayTag Channel, const FQuestGiverRegisteredEvent& Event);
	void HandleNodeDeactivationRequest(FGameplayTag Channel, const FQuestDeactivateRequestEvent& Event);
	void HandleNodeDeactivatedEvent(FGameplayTag Channel, const FQuestDeactivatedEvent& Event);
	void HandleActivationRequest(FGameplayTag Channel, const FQuestActivationRequestEvent& Event);
	void HandleBlockRequest(FGameplayTag Channel, const FQuestBlockRequestEvent& Event);
	void HandleClearBlockRequest(FGameplayTag Channel, const FQuestClearBlockRequestEvent& Event);
	void HandleResolveRequest(FGameplayTag Channel, const FQuestResolveRequestEvent& Event);
	void HandleQuestlineStartRequest(FGameplayTag Channel, const FQuestlineStartRequestEvent& Event);
	
	FDelegateHandle GivenDelegateHandle;
	FDelegateHandle GiverRegisteredDelegateHandle;
	FDelegateHandle DeactivateEventDelegateHandle;
	FDelegateHandle ActivationRequestDelegateHandle;
	FDelegateHandle BlockRequestDelegateHandle;
	FDelegateHandle ClearBlockRequestDelegateHandle;
	FDelegateHandle ResolveRequestDelegateHandle;
	FDelegateHandle QuestlineStartRequestDelegateHandle;

	TMap<FGameplayTag, FDelegateHandle> LiveStepTriggerHandles;

	/** Per-node FQuestDeactivatedEvent subscription handles; populated in ActivateQuestlineGraph, cleaned up in Deinitialize. */
	TMap<FGameplayTag, FDelegateHandle> DeactivationSubscriptionHandles;

	void SetQuestLive(FGameplayTag QuestTag);
	void SetQuestResolved(FGameplayTag QuestTag, FGameplayTag OutcomeTag, EQuestResolutionSource Source);
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
	
	TMultiMap<FGameplayTag, UClass*> ClassFilteredSteps;
	FDelegateHandle ClassBridgeHandle;

	void CheckClassObjectives(FGameplayTag Channel, const FInstancedStruct& RawEvent);


	/*------------------------------------------------------------------------------------------------------------------
	 * Deferred Completion
	 *----------------------------------------------------------------------------------------------------------------*/

	/**
	 * Pending completion data for a step whose chain is deferred until prereqs satisfy. Carries both the runtime
	 * OutcomeTag (event payload axis) and the PathIdentity (structural routing axis) so the resumed chain calls
	 * ChainToNextNodes with the same arguments the immediate path would have used.
	 */
	struct FQuestDeferredCompletion
	{
		FGameplayTag OutcomeTag;
		FName PathIdentity = NAME_None;
	};

	// Key: Step tag; Value: Pending completion data (OutcomeTag + PathIdentity)
	TMap<FGameplayTag, FQuestDeferredCompletion> DeferredCompletions;

	// Subscription handles for deferred completion prerequisite monitoring
	TMap<FGameplayTag, TMap<FGameplayTag, FPrereqLeafSubscription::FPrereqLeafHandles>> DeferredCompletionPrereqHandles;

	void DeferChainToNextNodes(UQuestStep* Step, FGameplayTag OutcomeTag, FName PathIdentity);
	void OnDeferredCompletionPrereqAdded(FGameplayTag Channel, const FWorldStateFactAddedEvent& Event);
	void OnDeferredCompletionPrereqResolutionRecorded(FGameplayTag Channel, const FQuestResolutionRecordedEvent& Event);
	void OnDeferredCompletionPrereqEntryRecorded(FGameplayTag Channel, const FQuestEntryRecordedEvent& Event);
	void TryFireDeferredCompletion(FGameplayTag StepTag);

	/** Shared body for all OnDeferredCompletionPrereq*** handlers: try every deferred completion. */
	void TryFireAllDeferredCompletions();

	
	/*------------------------------------------------------------------------------------------------------------------
	 * Enablement Watches: bidirectional state tracker per giver-gated quest in PendingGiver state.
	 *
	 * When a giver-gated quest's activation wire arrives and its prereq expression is non-Always, an entry is
	 * registered here that subscribes to each prereq leaf on both Added AND Removed events. On any leaf change,
	 * the watch re-evaluates the prereq and compares to its last-known state. Transitions fire FQuestEnabledEvent
	 * (unsatisfied to satisfied) or FQuestDisabledEvent (satisfied to unsatisfied). Designers binding to both events
	 * get bidirectional UI sync.
	 *
	 * Entries persist for the entire PendingGiver lifetime. Cleared on give success, abandon, or Deinitialize.
	 *----------------------------------------------------------------------------------------------------------------*/

	struct FEnablementWatch
	{
		FName NodeTagName;
		bool bLastKnownSatisfied = false;
	};

	TMap<FGameplayTag, FEnablementWatch> EnablementWatches;
	TMap<FGameplayTag, TMap<FGameplayTag, FPrereqLeafSubscription::FPrereqLeafHandles>> EnablementWatchHandles;

	/**
	 * Per-quest map of "the giver actor that initiated the most-recent successful give". Populated in
	 * HandleGiveQuestEvent right before ActivateNodeByTag, consumed in HandleOnNodeStarted to populate the
	 * GiverActor field on FQuestStartedEvent. Cleared on consumption so a subsequent non-giver activation
	 * doesn't inherit a stale entry.
	 */
	TMap<FGameplayTag, TWeakObjectPtr<AActor>> RecentGiverActors;

	void RegisterEnablementWatch(FGameplayTag QuestTag, FName NodeTagName, const FPrerequisiteExpression& Expr, bool bInitialSatisfied);
	void OnEnablementLeafFactAdded(FGameplayTag Channel, const FWorldStateFactAddedEvent& Event);
	void OnEnablementLeafFactRemoved(FGameplayTag Channel, const FWorldStateFactRemovedEvent& Event);
	void OnEnablementLeafResolutionRecorded(FGameplayTag Channel, const FQuestResolutionRecordedEvent& Event);
	void OnEnablementLeafEntryRecorded(FGameplayTag Channel, const FQuestEntryRecordedEvent& Event);
	void ReevaluateEnablementWatch(FGameplayTag QuestTag);
	void ClearEnablementWatch(FGameplayTag QuestTag);

	/** Shared body for all OnEnablementLeaf*** handlers: re-evaluate every active enablement watch.
	Per-channel filtering isn't worth the inverse-lookup cost; expression re-eval is cheap. */
	void ReevaluateAllEnablementWatches();
};
