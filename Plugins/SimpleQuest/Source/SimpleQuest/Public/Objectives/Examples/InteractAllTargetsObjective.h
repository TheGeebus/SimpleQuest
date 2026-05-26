// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Objectives/QuestObjective.h"
#include "Quests/Types/QuestRoleSourceInfo.h"
#include "InteractAllTargetsObjective.generated.h"


/**
 * Multi-target Objective: completes when every Trigger Component currently targeting the owning Step has fired
 * at least once. As each target ticks off the list, PublishTriggerSatisfied causes the matching Trigger
 * Component's OnQuestTriggerSatisfied delegate to fire so adopters can disable visuals / collision /
 * interaction prompts per-actor while the step continues for the remaining targets.
 *
 * Discovery: target set is populated at OnObjectiveActivated time via QSS GetTriggersTargetingThisStep, using
 * the OwningStepTag cached by the framework at activation dispatch. Adopters place Trigger Components in the
 * scene with StepTagsToTrigger including the Step's tag; the Objective auto-discovers them at runtime — no
 * authoring on the Step beyond the objective class assignment. Targeting actors are visible at edit time on
 * the Step's content-node Slate widget alongside the Givers.
 *
 * Use cases:
 *   - "Interact with N specific NPCs" — investigation, dialogue gating, evidence collection.
 *   - "Visit N locations" — exploration, scouting.
 *   - "Trigger N volumes in a level" — environmental puzzles.
 *
 * Adopter wiring on each target actor:
 *   1. Add a UQuestTriggerComponent (or subclass) with StepTagsToTrigger including the Step's tag.
 *   2. Call SendTriggerEvent from interaction code (BeginOverlap, Interact key, etc.).
 *   3. Optionally bind OnQuestTriggerSatisfied on the target actor's Trigger Component to drive per-target
 *      visual disable. Skipping the binding is fine — adopters who don't need the visual aren't forced to wire it.
 */
UCLASS()
class SIMPLEQUEST_API UInteractAllTargetsObjective : public UQuestObjective
{
	GENERATED_BODY()

public:
	UInteractAllTargetsObjective();

protected:
	virtual void OnObjectiveActivated_Implementation(const FQuestObjectiveActivationContext& Params) override;
	virtual void TryCompleteObjective_Implementation(const FQuestObjectiveTriggerContext& InContext) override;

	/**
	 * Outcome fired when every target in the discovered set has been interacted with. Single-outcome design;
	 * adopters wanting per-tick side-effects at the questline level can wire the per-target signal through
	 * UQuestTriggerComponent::OnQuestTriggerSatisfied on the target actors.
	 */
	UPROPERTY(EditDefaultsOnly, meta = (Categories = "SimpleQuest.Outcome", ObjectiveOutcome))
	FGameplayTag AllTargetsInteractedOutcomeTag;

private:
	/** Targets discovered at activation that have NOT yet fired. Drained as each target's interaction lands. */
	UPROPERTY()
	TSet<TWeakObjectPtr<AActor>> PendingTargetActors;

	/** Targets that HAVE fired. Inverse of PendingTargetActors. Carried for diagnostic / save-load purposes. */
	UPROPERTY()
	TSet<TWeakObjectPtr<AActor>> SatisfiedTargetActors;

	/**
	 * Strips dead weak refs from PendingTargetActors and returns the live count. Used to detect "all done"
	 * after a target actor was destroyed mid-step (never going to fire, shouldn't gate completion).
	 */
	int32 PruneAndCountPending();
};