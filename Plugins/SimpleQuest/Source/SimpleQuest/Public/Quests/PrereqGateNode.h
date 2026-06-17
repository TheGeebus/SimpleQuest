// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Quests/QuestNodeBase.h"
#include "Quests/Types/OriginatingEventID.h"
#include "PrereqGateNode.generated.h"

/**
 * Utility node that gates an activation cascade on a prerequisite expression. On activation, Super::Activate evaluates
 * PrerequisiteExpression; if satisfied, ActivateInternal fires Forward immediately. If unsatisfied, DeferActivation
 * subscribes to leaf channels and ActivateInternal fires Forward the moment the expression flips true. Compiled from
 * UQuestlineNode_PrereqGate via FQuestlineGraphCompiler::CompileUtilityNodes.
 *
 * Subscribes symmetrically so NOT(Fact) leaves wake on fact removal too — Fact leaves are reversible state by nature,
 * while Path / Resolution / Entry / Outcome leaves are append-only by registry shape and inherit no-op symmetric
 * subscription (the removed-event channels simply never fire for them).
 */
UCLASS()
class SIMPLEQUEST_API UPrereqGateNode : public UQuestNodeBase
{
	GENERATED_BODY()

	friend class FQuestlineGraphCompiler;

protected:
	virtual void ActivateInternal(FGameplayTag InContextualTag) override;
	virtual bool UseSymmetricPrereqSubscription() const override { return true; }

	/**
	 * PIE-session reset — clears LastFiredEventID so a fresh play session doesn't carry over the previous
	 * session's deduplication state. Always call Super first.
	 */
	virtual void ResetTransientState() override;

private:
	/**
	 * The OriginatingEventID this gate most recently fired for. Per-event-ID deduplication: when the same cascade
	 * event reaches the gate via both the prereq deferral path AND a direct Enter wire (e.g., a Step whose
	 * completion both satisfies the gate's last prereq AND cascades downstream to the gate), the gate fires
	 * once. Subsequent cascades (different OriginatingEventID) fire normally.
	 *
	 * Invalid by default; set after each successful fire. ResetTransientState clears it on PIE re-entry.
	 */
	FOriginatingEventID LastFiredEventID;
};