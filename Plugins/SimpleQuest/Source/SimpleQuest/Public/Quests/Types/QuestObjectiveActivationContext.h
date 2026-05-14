// Copyright 2026, Greg Bussell, All Rights Reserved.

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
 */
USTRUCT(BlueprintType)
struct SIMPLEQUEST_API FQuestObjectiveActivationContext
{
	GENERATED_BODY()

	/** Design-time configuration set on the destination Step. Immutable per activation. */
	UPROPERTY(BlueprintReadWrite)
	FQuestObjectiveAuthoredConfig Authored;

	/** Runtime-accumulated context from upstream contributions. */
	UPROPERTY(BlueprintReadWrite)
	FQuestObjectiveRuntimeContext Dynamic;
};