// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Quests/Types/QuestObjectiveAuthoredConfig.h"
#include "Quests/Types/QuestObjectiveRuntimeContext.h"
#include "QuestObjectiveActivationContext.generated.h"

/**
 * Top-level activation context delivered to UQuestObjective::OnObjectiveActivated.
 *
 *   - Authored : FQuestObjectiveAuthoredConfig
 *       Design-time configuration set on the destination Step. Immutable per activation.
 *   - Dynamic : FQuestObjectiveRuntimeContext
 *       Runtime-accumulated context from upstream contributions. Derives from FQuestContextBase so
 *       inherits Instigator, CustomData, lineage uniformly.
 *
 * Example usage:
 *   void UMyObjective::OnObjectiveActivated_Implementation(const FQuestObjectiveActivationContext& Context)
 *   {
 *       const auto& AuthoredClasses = Context.Authored.TargetClasses;       // designer-pinned
 *       const auto& InjectedActors  = Context.Dynamic.TargetActors;         // runtime-supplied
 *       const auto* MyConfig        = Cast<UMyObjectiveConfig>(             // typed designer config
 *           Context.Authored.ConfigAsset.LoadSynchronous());
 *       const auto* MyUpstreamData  = Context.Dynamic.CustomData.GetPtr<FMyUpstreamData>();  // typed runtime
 *   }
 *
 * UPCOMING CHANGE (0.5.0): this nested-struct shape is scheduled for restructure. The Authored / Dynamic
 * split moves from a single nested type onto the OnObjectiveActivated signature itself (two parameters —
 * FQuestObjectiveAuthoredConfig& Authored, FQuestObjectiveRuntimeContext& Runtime), pushing the split
 * from a data shape onto the consumer boundary. The field semantics on each sub-struct stay the same;
 * only the delivery container changes. Adopter override sites need to update their signature when 0.5.0
 * lands.
 */
USTRUCT(BlueprintType)
struct SIMPLEQUEST_API FQuestObjectiveActivationContext
{
	GENERATED_BODY()

	/**
	 * Design-time configuration set on the destination Step. Immutable per activation.
	 *
	 * UPCOMING CHANGE (0.5.0): this field is removed when OnObjectiveActivated's signature restructures
	 * to deliver Authored as a top-level parameter. Field semantics unchanged; access path changes from
	 * Context.Authored.X to Authored.X.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FQuestObjectiveAuthoredConfig Authored;

	/**
	 * Runtime-accumulated context from upstream contributions.
	 *
	 * UPCOMING CHANGE (0.5.0): this field is removed when OnObjectiveActivated's signature restructures
	 * to deliver Runtime as a top-level parameter. Field semantics unchanged; access path changes from
	 * Context.Dynamic.X to Runtime.X. Note the parameter rename — Runtime more accurately reflects the
	 * contents than Dynamic did.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FQuestObjectiveRuntimeContext Dynamic;
};