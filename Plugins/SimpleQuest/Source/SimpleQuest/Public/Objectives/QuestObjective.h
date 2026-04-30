// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Quests/Types/QuestObjectiveActivationParams.h"
#include "Quests/Types/QuestObjectiveContext.h"
#include "QuestObjective.generated.h"

class UQuestTargetInterface;
class IQuestTargetInterface;
struct FQuestObjectiveActivationParams;

/**
 * Base class with functions intended to be overridden to provide logic for the completion of a given quest step.
 *
 * Outcome tags are discovered automatically by the editor from two primary sources on subclasses:
 * K2 node scan (Blueprint) and UPROPERTY reflection (C++). A virtual fallback (GetPossibleOutcomes)
 * is available for programmatic or dynamic cases. See GetPossibleOutcomes for full documentation.
 */
UCLASS(Blueprintable)
class SIMPLEQUEST_API UQuestObjective : public UObject
{
	GENERATED_BODY()

public:
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnEnableTarget, UObject*, InTargetObject, bool, bNewIsEnabled);
	FOnEnableTarget OnEnableTarget;

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FQuestObjectiveComplete, FGameplayTag, OutcomeTag, FName, PathIdentity);
	FQuestObjectiveComplete OnQuestObjectiveComplete;
	
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnQuestObjectiveProgress, FQuestObjectiveContext, ProgressContext);
	FOnQuestObjectiveProgress OnQuestObjectiveProgress;
	
	/**
	 * Outcome Tag Discovery																						<br>
	 * ---------------------																						<br>
	 * The editor discovers outcome tags from two primary sources. All results merge
	 * into a single deduplicated set — pins on the Step node reflect the union.
	 *
	 * 1. K2 Node Scan (Blueprint subclasses):																		
	 *    - Place UK2Node_CompleteObjectiveWithOutcome nodes in event graphs.							
	 *    - Each node's OutcomeTag is discovered automatically.
	 *
	 * 2. UPROPERTY Reflection (C++ subclasses):																	
	 *    - Declare FGameplayTag properties with the ObjectiveOutcome metadata specifier.				
	 *    - Back each tag with UE_DEFINE_GAMEPLAY_TAG to guarantee registration before CDO
	 *      construction. Bare RequestGameplayTag in a constructor will fail if the tag
	 *      is not yet loaded from INI at module init time.
	 *    - The editor discovers tagged properties via TFieldIterator reflection scan.
	 *																									
	 *        - In the header:																						<br>
	 *			   UPROPERTY(EditDefaultsOnly, meta = (Categories = "SimpleQuest.QuestOutcome", ObjectiveOutcome))	<br>
	 *			   FGameplayTag Outcome_Reached;	
	 *																									
	 *        - In the .cpp (file scope):																			<br>
	 *			   UE_DEFINE_GAMEPLAY_TAG(Tag_Outcome_Reached, "SimpleQuest.QuestOutcome.Reached")
	 *
	 *        - In the constructor:																					<br>
	 *			   Outcome_Reached = Tag_Outcome_Reached;
	 *
	 *        - In TryCompleteObjective:																			<br>
	 *			   CompleteObjectiveWithOutcome(Outcome_Reached);											
	 *																									
	 *    - Categories="Quest.Outcome" filters the tag picker to the outcome namespace.					
	 *    - ObjectiveOutcome marks the property for discovery — no value needed,
	 *      presence is sufficient.
	 *
	 * Both sources are additive across the inheritance chain. A Blueprint subclass of a
	 * C++ class that declares ObjectiveOutcome properties will produce pins for both sets combined.
	 * Use 'Add Call to Parent Function' context menu option by right-clicking the event node to add a
	 * call to any C++ implementation on the appropriate branch in the child Objective blueprint.
	 *
	 * Override this virtual as a fallback for programmatic or dynamic outcomes that cannot be
	 * expressed as individual UPROPERTY members or K2 nodes — e.g. configuration-driven outcomes
	 * computed at CDO construction time. Tags returned here are not constrained to the
	 * SimpleQuest.QuestOutcome namespace. Base implementation returns an empty array.
	 *
	 * @see FSimpleQuestEditorUtilities::DiscoverObjectivePaths
	 * @see UK2Node_CompleteObjectiveWithOutcome
	 */
	virtual TArray<FGameplayTag> GetPossibleOutcomes() const;

	/**
	 * Step-facing entry point for initializing objective target parameters. Thin C++ forwarder to the protected
	 * BlueprintNativeEvent SetObjectiveTarget — routes through the engine's UFunction thunk so BP overrides in
	 * subclass objectives fire correctly. Not UFUNCTION; intentionally invisible to BP.
	 */
	void DispatchOnObjectiveActivated(const FQuestObjectiveActivationParams& Params);

	/**
	 * Manager-facing entry point for triggering objective evaluation. Thin C++ forwarder to the protected
	 * BlueprintNativeEvent TryCompleteObjective. Same thunk-routing behavior as DispatchSetObjectiveTarget.
	 */
	void DispatchTryCompleteObjective(const FQuestObjectiveContext& InContext);

	/**
	 * Step-facing entry point for tearing down the objective. Thin C++ forwarder to the protected
	 * BlueprintNativeEvent OnObjectiveDeactivated. Same thunk-routing behavior as DispatchOnObjectiveActivated.
	 *
	 * Called by UQuestStep BOTH on the interruption path (DeactivateInternal: abandon, blocked, cascade-
	 * deactivated) AND on the completion path (OnObjectiveComplete) before the step releases its reference
	 * to the objective. Gives subclasses a symmetric hook to OnObjectiveActivated for unsubscribing from
	 * external event sources, releasing UI handles, stopping timers, etc.
	 *
	 * Does NOT fire on PIE-end / ResetTransientState, the objective is already GC'd at that point.
	 * Subclasses subscribing to non-UE systems should defend against PIE end via TWeakObjectPtr or
	 * equivalent standard UE patterns.
	 */
	void DispatchOnObjectiveDeactivated();
	
protected:
	/**
	 * Set the initial conditions for the quest step. This event may be overridden to provide a convenient place
	 * to bind additional delegates. (see: UGoToQuestObjective)
	 *
	 * BlueprintProtected: not callable from BP outside the UQuestObjective class hierarchy. Call via the public
	 * DispatchSetObjectiveTarget from C++; subclass BPs override normally (the Override dropdown still lists it).
	 *
	 * @param Params a set of specific target actors in the scene
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, meta = (BlueprintProtected = "true"), Category = "Quest|Objectives")
	void OnObjectiveActivated(const FQuestObjectiveActivationParams& Params);

	/**
	 * Symmetric partner to OnObjectiveActivated. Fires whenever the owning step is releasing the
	 * objective: both the interruption path (abandon, blocked, cascade-deactivated) AND the completion
	 * path. Override to unsubscribe from external event sources, tear down UI handles, release timers, etc.
	 *
	 * The objective is still live when this fires; LiveObjective on the step is nulled AFTER this dispatch
	 * returns. Inside the override you can still call EnableQuestTargetActors(false), inspect TargetActors,
	 * read state stored during OnObjectiveActivated, etc.
	 *
	 * BlueprintProtected: not callable from BP outside the UQuestObjective class hierarchy. Call via the
	 * public DispatchOnObjectiveDeactivated from C++; subclass BPs override normally.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, meta = (BlueprintProtected = "true"), Category = "Quest|Objectives")
	void OnObjectiveDeactivated();

	/**
	 * Count a relevant quest target and determine if the step should end in success or failure. This event is intended
	 * to be overridden by child classes to provide the logic for quest step completion.
	 *
	 * This function should be used to count elements or perform additional logic and call CompleteObjectiveWithOutcome
	 * (via the K2 Complete Objective node in BP) to signal completion. Base class has no default implementation; override
	 * in C++ or Blueprint subclasses.
	 *
	 * BlueprintProtected — not callable from BP outside the UQuestObjective class hierarchy. Call via the public
	 * DispatchTryCompleteObjective from C++; subclass BPs override normally.
	 *
	 * Example child objectives: UGoToQuestObjective and UKillClassQuestObjective
	 * @param InContext the context of this trigger event containing the triggered actor, any relevant instigator, and an
	 *					optional designer-defined instanced struct that may contain additional fields as needed.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, meta = (BlueprintProtected = "true"), Category = "Quest|Objectives")
	void TryCompleteObjective(const FQuestObjectiveContext& InContext);
	
	/**
	 * Completes the objective with a runtime outcome tag and optional explicit path identity. PathIdentity routes
	 * the completion through the Step's structurally-keyed pin map (NextNodesByPath); when NAME_None, the manager
	 * auto-derives PathIdentity from OutcomeTag.GetTagName() so static K2 placements behave identically to pre-
	 * Bundle-Y. Dynamic K2 placements supply an explicit PathIdentity authored on the K2 node's PathName field.
	 *
	 * Direct C++ callers can also supply PathIdentity explicitly; legacy callers passing only OutcomeTag get the
	 * auto-derive fallback via the default argument.
	 */
	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", AutoCreateRefTerm = "InCompletionContext,InForwardParams"), Category = "Quest|Objectives")
	void CompleteObjectiveWithOutcome(FGameplayTag OutcomeTag, FName PathIdentity = NAME_None, const FQuestObjectiveContext& InCompletionContext = FQuestObjectiveContext(), const FQuestObjectiveActivationParams& InForwardParams = FQuestObjectiveActivationParams());
	
	/**
	 * Fires OnQuestObjectiveProgress. Step forwards to manager, which publishes FQuestProgressEvent on the step tag channel.
	 * Use this directly for objectives with custom progress logic (multi-counter, phase-based, etc.).
	 */
	UFUNCTION(BlueprintCallable, Category = "Quest|Objectives")
	void ReportProgress(const FQuestObjectiveContext& ProgressContext);
	
	UFUNCTION(BlueprintCallable, Category = "Quest|Objectives")
	void EnableTargetObject(UObject* Target, bool bIsTargetEnabled) const;

	UFUNCTION(BlueprintCallable, Category = "Quest|Objectives")
	void EnableQuestTargetActors(bool bIsTargetEnabled);

	UFUNCTION(BlueprintCallable, Category = "Quest|Objectives")
	void EnableQuestTargetClasses(bool bIsTargetEnabled) const;
	
private:
	/** Set by CompleteObjectiveWithOutcome. Read by the step via TakeCompletionContext. */
	UPROPERTY()
	FQuestObjectiveContext CompletionContext;
	
	/**
	 * Optional designer-supplied params to forward to downstream step activations on completion. Read by the step
	 * via TakeForwardActivationParams. Empty (default) is the common case — in which only the chain propagation
	 * fields get forwarded on handoff.
	 */
	UPROPERTY()
	FQuestObjectiveActivationParams ForwardActivationParams;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = true), Category = Targets)
	TSet<TSoftObjectPtr<AActor>> TargetActors;
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = true), Category = Targets)
	TSet<TSoftClassPtr<AActor>> TargetClasses;
	
public:
	FORCEINLINE const TSet<TSoftObjectPtr<AActor>>& GetTargetActors() const { return TargetActors; }
	FORCEINLINE const TSet<TSoftClassPtr<AActor>>& GetTargetClasses() const { return TargetClasses; }
	FQuestObjectiveContext TakeCompletionContext() { return MoveTemp(CompletionContext); }
	FQuestObjectiveActivationParams TakeForwardActivationParams() { return MoveTemp(ForwardActivationParams); }
};
