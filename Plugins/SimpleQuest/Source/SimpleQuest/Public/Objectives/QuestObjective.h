// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Quests/Types/QuestObjectiveActivationContext.h"
#include "Quests/Types/QuestObjectiveTriggerContext.h"
#include "Quests/Types/QuestRoleSourceInfo.h"
#include "QuestObjective.generated.h"


struct FQuestObjectiveActivationContext;


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
	
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnQuestObjectiveProgress, FQuestObjectiveTriggerContext, ProgressContext);
	FOnQuestObjectiveProgress OnQuestObjectiveProgress;

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnQuestObjectiveRefused, FGameplayTag, RefusalReason, FQuestObjectiveTriggerContext, TriggerContext);
		
	/**
	 * Fired by RefuseTrigger when the objective sees a SendTriggerEvent fire but declines to act on it. Step subscribes
	 * and forwards to the manager, which publishes FQuestTriggerResponseEvent(Refused) on the step's tag channel so
	 * adopters bound to UQuestTriggerComponent::OnQuestTriggerResponded receive the refusal with the originating
	 * TriggerContext echoed back for own-fire filtering.
	 */
	FOnQuestObjectiveRefused OnQuestObjectiveRefused;

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnQuestObjectiveTriggerDeactivation, FGameplayTag, OutcomeTag, FQuestObjectiveTriggerContext, FinalContext);
	
	/**
	 * Fired by PublishTriggerDeactivation when the objective explicitly signals "I'm done consuming fires on this trigger"
	 * without ending the step. Step subscribes and forwards to the manager, which publishes FQuestTriggerDeactivatedEvent
	 * with EndReason = Manual. Distinct from the auto-publish path (Completed / Interrupted) that fires alongside the
	 * step's lifecycle transitions.
	 */
	FOnQuestObjectiveTriggerDeactivation OnQuestObjectiveTriggerDeactivation;

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnQuestObjectiveTriggerSatisfied, FQuestObjectiveTriggerContext, Context);

	/**
	 * Fired by PublishTriggerSatisfied when the Objective marks a specific Trigger Component's actor as having
	 * been consumed by a multi-target satisfaction list. Step subscribes and forwards to the manager, which
	 * publishes FQuestTriggerSatisfiedEvent on the step's tag channel. Trigger Components subscribe to that
	 * event and filter by own-actor to react per-target.
	 */
	FOnQuestObjectiveTriggerSatisfied OnQuestObjectiveTriggerSatisfied;
	
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
	 *			   UPROPERTY(EditDefaultsOnly, meta = (Categories = "SimpleQuest.Outcome", ObjectiveOutcome))	<br>
	 *			   FGameplayTag Outcome_Reached;	
	 *																									
	 *        - In the .cpp (file scope):																			<br>
	 *			   UE_DEFINE_GAMEPLAY_TAG(Tag_Outcome_Reached, "SimpleQuest.Outcome.Reached")
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
	 * SimpleQuest.Outcome namespace. Base implementation returns an empty array.
	 *
	 * @see FSimpleQuestEditorUtilities::DiscoverObjectivePaths
	 * @see UK2Node_CompleteObjectiveWithOutcome
	 */
	virtual TArray<FGameplayTag> GetPossibleOutcomes() const;

	// ── §4.33 Phase 1 — runtime self-introspection ─────────────────────────────────────────────────────────
	//
	// Lets an Objective discover its own identity in the framework's address space and find authored actors
	// targeting its Step. OwningStepTag is cached at activation dispatch (lifetime-stable — the Objective
	// doesn't migrate between Steps). Alias-tag and trigger / giver lookups chain through the QuestState-
	// Subsystem's existing query surface; no per-instance caching for the rare-cheap cases.

	/**
	 * The ContextualTag of the Step that hosts this Objective. Valid from OnObjectiveActivated onward; empty
	 * after OnObjectiveDeactivated.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Quest|Objectives")
	FGameplayTag GetOwningStepTag() const { return OwningStepTag; }

	/**
	 * Returns the AssetScopedAliasTags for this Objective's owning Step — every additional perspective tag
	 * that LinkedQuestline ancestors have registered for the Step. Empty for top-level content (typical case).
	 * Uncached — lookup-per-call via QSS's existing alias index.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Quest|Objectives")
	TArray<FGameplayTag> GetOwningStepAliasTags() const;

	/**
	 * Convenience: every Trigger source currently registered against this Objective's owning Step (canonical
	 * + aliases). Pure composition over QuestStateSubsystem::GetActiveTriggersForTag — closes the "where are
	 * the actors targeting me?" loop without adopters maintaining a parallel tag → actor registry.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Quest|Objectives")
	TArray<FQuestRoleSourceInfo> GetTriggersTargetingThisStep() const;

	/** Convenience: every Giver source currently registered against this Objective's owning Step. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Quest|Objectives")
	TArray<FQuestRoleSourceInfo> GetGiversTargetingThisStep() const;

	/**
	 * Step-facing entry point for initializing objective target parameters. Thin C++ forwarder to the protected
	 * BlueprintNativeEvent SetObjectiveTarget — routes through the engine's UFunction thunk so BP overrides in
	 * subclass objectives fire correctly. Not UFUNCTION; intentionally invisible to BP.
	 */
	void DispatchOnObjectiveActivated(const FQuestObjectiveActivationContext& Params, FGameplayTag InOwningStepTag);

	/**
	 * Manager-facing entry point for triggering objective evaluation. Thin C++ forwarder to the protected
	 * BlueprintNativeEvent TryCompleteObjective. Same thunk-routing behavior as DispatchSetObjectiveTarget.
	 */
	void DispatchTryCompleteObjective(const FQuestObjectiveTriggerContext& InContext);

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
	 * UPCOMING CHANGE (0.5.0): the single-parameter signature here is scheduled for restructure into a two-
	 * parameter shape — (FQuestObjectiveAuthoredConfig& Authored, FQuestObjectiveRuntimeContext& Runtime) —
	 * pushing the Authored/Runtime split from a nested data shape onto the consumer boundary. Adopter override
	 * sites will need to update their signature (FunctionRedirects can't auto-fix signature changes). Activation
	 * payload data remains intact; the structural change is purely how the parts are delivered. Save/load
	 * benefits from the stable shape, which is why the restructure lands alongside that release.
	 *
	 * @param Params a set of specific target actors in the scene
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, meta = (BlueprintProtected = "true"), Category = "Quest|Objectives")
	void OnObjectiveActivated(const FQuestObjectiveActivationContext& Params);

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
	 * Count a relevant quest trigger and determine if the step should end in success or failure. This event is intended
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
	void TryCompleteObjective(const FQuestObjectiveTriggerContext& InContext);
	
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
	void CompleteObjectiveWithOutcome(FGameplayTag OutcomeTag, FName PathIdentity = NAME_None, const FQuestObjectiveTriggerContext& InCompletionContext = FQuestObjectiveTriggerContext(), const FQuestObjectiveActivationContext& InForwardParams = FQuestObjectiveActivationContext());
	
	/**
	 * Broadcasts an objective-progress signal to the framework via OnQuestObjectiveProgress. Progress events
	 * are PURELY EXPLICIT — the framework never auto-fires Progress as part of completion or any other
	 * implicit lifecycle path. Objectives that want a "final X/X tick" before completing call ReportProgress
	 * with the final state, then CompleteObjectiveWithOutcome.
	 *
	 * Some objectives have no per-fire progress semantic at all (e.g., a binary "interacted yes/no" objective)
	 * and never call this method. Listeners receive zero Progress events for those objectives, exactly as
	 * intended — no event-shape filtering required at the listener side.
	 */
	UFUNCTION(BlueprintCallable, Category = "Quest|Objectives")
	void ReportProgress(const FQuestObjectiveTriggerContext& ProgressContext);
	
	/**
	 * Declines a trigger fire that reached this objective. Step forwards to manager, which publishes
	 * FQuestTriggerResponseEvent(Refused) on the step's tag channel. Use from within TryCompleteObjective when an
	 * incoming fire doesn't satisfy game-logic conditions (wrong actor, missing item, wrong phase) and should be
	 * surfaced to the trigger actor's UI rather than silently dropped.
	 *
	 * RefusalReason is designer-defined and consumed by the trigger actor's response handler — typically a
	 * SimpleQuest.Refusal.* descendant or any tag whose subscribers know how to translate to UI / audio.
	 *
	 * Echoes the originating TriggerContext through to the published event so UQuestTriggerComponent's filter
	 * (TriggeredActor == GetOwner()) resolves correctly. Adopters should typically pass the InContext their
	 * TryCompleteObjective override received, not construct a fresh empty one.
	 */
	UFUNCTION(BlueprintCallable, meta = (BlueprintProtected = "true", AutoCreateRefTerm = "TriggerContext"), Category = "Quest|Objectives")
	void RefuseTrigger(FGameplayTag RefusalReason, const FQuestObjectiveTriggerContext& TriggerContext);

	/**
	 * Manually signals "the trigger side of this step's lifecycle is wrapping" without affecting the step's own
	 * lifecycle. Step forwards to manager, which publishes FQuestTriggerDeactivatedEvent with EndReason = Manual.
	 * Distinct from the auto-publish path fired by the manager adjacent to FQuestEndedEvent / FQuestDeactivatedEvent
	 * (EndReason = Completed / Interrupted) — Manual fires don't change the step's state, only notify trigger-side
	 * subscribers.
	 *
	 * Use when an objective wants to release trigger-side audiences early — e.g., a multiphase objective that's
	 * done with one trigger volume's input but isn't yet ready to complete the step.
	 */
	UFUNCTION(BlueprintCallable, meta = (BlueprintProtected = "true", AutoCreateRefTerm = "FinalContext"), Category = "Quest|Objectives")
	void PublishTriggerDeactivation(FGameplayTag OutcomeTag, const FQuestObjectiveTriggerContext& FinalContext);

	/**
	 * Signals that a specific Trigger Component's actor has been consumed by a multi-target satisfaction list. Step
	 * forwards to manager, which publishes FQuestTriggerSatisfiedEvent on the step's tag channel with the satisfied
	 * actor's reference. Trigger Components watching the step filter by SatisfiedActor == GetOwner() and broadcast
	 * their OnQuestTriggerSatisfied delegate.
	 *
	 * Distinct from PublishTriggerDeactivation: that signals the trigger SIDE wrapping for all watching components;
	 * this signals one specific actor's contribution is consumed while the step continues for other targets. Pair
	 * with a final CompleteObjectiveWithOutcome call once every target has been satisfied.
	 *
	 * Use from a multi-target Objective subclass: in TryCompleteObjective_Implementation, identify the firing actor,
	 * check it against the still-pending target set, and if matched call this with the context. Echoes the trigger
	 * context through to the published event so own-fire filtering symmetry holds.
	 */
	UFUNCTION(BlueprintCallable, meta = (BlueprintProtected = "true", AutoCreateRefTerm = "TriggerContext"), Category = "Quest|Objectives")
	void PublishTriggerSatisfied(const FQuestObjectiveTriggerContext& TriggerContext);
	
	UFUNCTION(BlueprintCallable, Category = "Quest|Objectives")
	void EnableTargetObject(UObject* Target, bool bIsTargetEnabled) const;

	UFUNCTION(BlueprintCallable, Category = "Quest|Objectives")
	void EnableQuestTargetActors(bool bIsTargetEnabled);

	UFUNCTION(BlueprintCallable, Category = "Quest|Objectives")
	void EnableQuestTargetClasses(bool bIsTargetEnabled) const;
	
private:
	/** Set by CompleteObjectiveWithOutcome. Read by the step via TakeCompletionContext. */
	UPROPERTY()
	FQuestObjectiveTriggerContext CompletionContext;
	
	/**
	 * Cached InContext from the most-recent DispatchTryCompleteObjective call. Used by ResolveTriggerContext to
	 * auto-forward TriggeredActor / Instigator / CustomData when adopters call ReportProgress / Complete /
	 * RefuseTrigger / PublishTriggerDeactivation without passing context — AND to force the framework-stamped
	 * OriginatingTriggerComponent through immune to adopter mutation, guaranteeing the publishing trigger
	 * component receives its own feedback.
	 */
	UPROPERTY()
	FQuestObjectiveTriggerContext LastTriggerContext;

	/**
	 * Field-level overlay: returns AdopterContext with TriggeredActor / Instigator / CustomData fallback-populated
	 * from LastTriggerContext where the adopter left them unset, AND OriginatingTriggerComponent force-overridden
	 * from LastTriggerContext unconditionally. Adopters who pass explicit attribution win on the first three fields;
	 * the originating-component identity stays framework-owned so own-fire filtering can't be broken by adopter
	 * re-targeting of TriggeredActor for game-logic reasons.
	 */
	FQuestObjectiveTriggerContext ResolveTriggerContext(const FQuestObjectiveTriggerContext& AdopterContext) const;
	
	/**
	 * Optional designer-supplied params to forward to downstream step activations on completion. Read by the step
	 * via TakeForwardActivationParams. Empty (default) is the common case — in which only the chain propagation
	 * fields get forwarded on handoff.
	 */
	UPROPERTY()
	FQuestObjectiveActivationContext ForwardActivationParams;

	/**
	 * Cached identity of the Step that hosts this Objective. Set by UQuestStep::ActivateInternal via
	 * DispatchOnObjectiveActivated before the BP event fires; cleared in DispatchOnObjectiveDeactivated.
	 * Owning Step doesn't migrate during the Objective's lifetime — single cache, trivial property getter.
	 */
	UPROPERTY(VisibleAnywhere)
	FGameplayTag OwningStepTag;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = true), Category = Targets)
	TSet<TSoftObjectPtr<AActor>> TargetActors;
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = true), Category = Targets)
	TSet<TSoftClassPtr<AActor>> TargetClasses;
	
public:
	FORCEINLINE const TSet<TSoftObjectPtr<AActor>>& GetTargetActors() const { return TargetActors; }
	FORCEINLINE const TSet<TSoftClassPtr<AActor>>& GetTargetClasses() const { return TargetClasses; }
	FQuestObjectiveTriggerContext TakeCompletionContext() { return MoveTemp(CompletionContext); }
	FQuestObjectiveActivationContext TakeForwardActivationParams() { return MoveTemp(ForwardActivationParams); }
};
