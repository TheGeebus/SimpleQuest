// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "QuestStepEnums.generated.h"

UENUM(BlueprintType)
enum class EPrerequisiteGateMode : uint8
{
	/** Trigger events are silently ignored while prerequisites are unmet. Player must interact again after prerequisites satisfy. */
	GatesProgression	UMETA(DisplayName = "Gate Progression"),

	/** Trigger events count normally. If completion conditions are already met when prerequisites satisfy, the step completes immediately. */
	GatesCompletion		UMETA(DisplayName = "Gate Completion"),
};

/**
 * Per-run resettability of a node's structural resolution (Path / Resolution / Entry), for replayable content.
 * Tri-state inherited property: a node takes the nearest ancestor container's effective setting unless it overrides.
 * Default Inherit; an unconfigured root resolves to Off (permanent — current behavior). When effectively On, the
 * node's resolution is mirrored to a clearable WorldState fact (the per-run projection) alongside the permanent
 * registry record, and prereq gates wired from it read that mirror — so the gate re-gates when the run resets while
 * the historical record is never touched. No effect on context-free Outcome leaves (irrevocable history) or raw
 * Fact leaves (already author-managed).
 */
UENUM(BlueprintType)
enum class EResettableReplay : uint8
{
	/** Take the nearest ancestor container's effective setting. Root default resolves to Off (permanent). */
	Inherit		UMETA(DisplayName = "Inherit"),

	/** Opt this node (and inheriting descendants) into per-run resettable gating. */
	Enabled		UMETA(DisplayName = "On"),

	/** Override out: this node's resolution stays permanent regardless of any ancestor's On. */
	Disabled	UMETA(DisplayName = "Off"),
};