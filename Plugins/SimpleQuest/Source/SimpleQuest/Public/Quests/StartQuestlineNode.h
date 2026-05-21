// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Quests/QuestNodeBase.h"
#include "Quests/Types/QuestObjectiveActivationContext.h"
#include "StartQuestlineNode.generated.h"

class UQuestlineGraph;

/**
 * Utility node that activates a questline graph asset at runtime. Publishes on Tag_Channel_QuestlineStart-
 * Request when activated; manager async-loads the graph and applies Params to the entry Step's activation.
 * BP-graph-native counterpart to USimpleQuestBlueprintLibrary::StartQuestline.
 */
UCLASS()
class SIMPLEQUEST_API UStartQuestlineNode : public UQuestNodeBase
{
	GENERATED_BODY()

	friend class FQuestlineGraphCompiler;

protected:
	/** Questline graph asset to activate. Loaded asynchronously; activation fires when the load resolves. */
	UPROPERTY()
	TSoftObjectPtr<UQuestlineGraph> Graph;

	/**
	 * Activation context applied to the questline's entry. Dynamic.Instigator / CustomData / OriginTag carry
	 * attribution; Authored fields are the destination Step's authoring concern (set on the Step, not here).
	 * Empty default merges cleanly with the entry Step's authored defaults.
	 */
	UPROPERTY()
	FQuestObjectiveActivationContext Params;

	virtual void ActivateInternal(FGameplayTag InContextualTag) override;
};