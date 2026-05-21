// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Utilities/QuestActivationGuard.h"

#include "Quests/Quest.h"
#include "Quests/QuestNodeBase.h"
#include "Utilities/QuestLifecycleQuery.h"


namespace FQuestActivationGuard
{
    EQuestActivationGuardDecision DecideFromInputs(const FQuestActivationGuardInputs& In)
    {
        // Diamond - Step refusal. Step Live or PendingGiver corrupts lifecycle invariants
        // on re-entry; the caller logs the refusal and returns without touching state.
        if (!In.bIsContainer && In.bIsLive)
        {
            return EQuestActivationGuardDecision::RefuseStepAlreadyLive;
        }
        if (!In.bIsContainer && In.bIsPendingGiver)
        {
            return EQuestActivationGuardDecision::RefuseStepAlreadyPendingGiver;
        }

        // Container reentry tracker - returned at the end if no other decision fires. A
        // re-entering container with a registered giver may still hit GiverGateFire or the
        // path-aware skip; a re-entering container that's Blocked still falls through to
        // the Block check below (Block remains a valid refusal even on container reentry).
        const bool bContainerReentry = In.bIsContainer && (In.bIsLive || In.bIsPendingGiver);

        // Giver gate. Bypass flag is set by the give-completion re-activation (player has
        // already accepted, route past the gate). Otherwise, registered-giver tags route
        // into either the path-aware skip or the gate fire.
        if (!In.bBypassGiverGate && In.bHasRegisteredGiver)
        {
            if (In.bAllReachableStepsAlreadyLive)
            {
                return EQuestActivationGuardDecision::GiverGateSkipPathAware;
            }
            return EQuestActivationGuardDecision::GiverGateFire;
        }

        // Block gate. Reached only when the giver gate didn't fire - blocking a giver-
        // gated quest still publishes FQuestActivatedEvent and keeps the giver visible /
        // interactive (Block is a re-initiation gate, not a UI suppressor).
        if (In.bIsBlocked)
        {
            return EQuestActivationGuardDecision::RefuseBlocked;
        }

        return bContainerReentry
            ? EQuestActivationGuardDecision::ContainerReentry
            : EQuestActivationGuardDecision::Proceed;
    }

    EQuestActivationGuardDecision Evaluate(
        const UWorldStateSubsystem* WS,
        const UQuestNodeBase* Instance,
        FGameplayTag NodeTag,
        FGameplayTag IncomingOutcomeTag,
        bool bBypassGiverGate,
        bool bHasRegisteredGiver)
    {
        if (!Instance || !NodeTag.IsValid())
        {
            // Preserves ActivateNodeByTag's degenerate-case behavior - invalid tag falls
            // through to the downstream Activate call without firing any guard.
            return EQuestActivationGuardDecision::Proceed;
        }

        FQuestActivationGuardInputs Inputs;
        Inputs.bIsContainer        = Instance->IsContainerNode();
        Inputs.bIsLive             = FQuestLifecycleQuery::IsLive(WS, NodeTag);
        Inputs.bIsPendingGiver     = FQuestLifecycleQuery::IsPendingGiver(WS, NodeTag);
        Inputs.bIsBlocked          = FQuestLifecycleQuery::IsBlocked(WS, NodeTag);
        Inputs.bBypassGiverGate    = bBypassGiverGate;
        Inputs.bHasRegisteredGiver = bHasRegisteredGiver;

        // Path-aware reachability - only matters for Container + has-giver + !bypass. Look
        // up the entered Activate pin's reachable Step subset (outcome-keyed first, falling
        // back to Any-Outcome / NAME_None), then probe each Step's Live fact. All-live
        // means the giver has no work to enable; gate skips.
        if (Inputs.bIsContainer && Inputs.bHasRegisteredGiver && !Inputs.bBypassGiverGate)
        {
            if (const UQuest* Container = Cast<UQuest>(Instance))
            {
                const FName PinKey = IncomingOutcomeTag.IsValid() ? IncomingOutcomeTag.GetTagName() : NAME_None;
                const FQuestReachableSteps* Reachable = Container->GetReachableStepsByActivatePin().Find(PinKey);
                if (!Reachable)
                {
                    Reachable = Container->GetReachableStepsByActivatePin().Find(NAME_None);
                }
                if (Reachable && !Reachable->StepTags.IsEmpty())
                {
                    Inputs.bAllReachableStepsAlreadyLive = true;
                    for (const FGameplayTag& StepTag : Reachable->StepTags)
                    {
                        if (!FQuestLifecycleQuery::IsLive(WS, StepTag))
                        {
                            Inputs.bAllReachableStepsAlreadyLive = false;
                            break;
                        }
                    }
                }
            }
        }

        return DecideFromInputs(Inputs);
    }
}

