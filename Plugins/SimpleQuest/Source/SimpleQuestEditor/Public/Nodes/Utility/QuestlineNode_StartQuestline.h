// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Nodes/Utility/QuestlineNode_UtilityBase.h"
#include "Quests/Types/QuestObjectiveActivationContext.h"
#include "QuestlineNode_StartQuestline.generated.h"

class UQuestlineGraph;

/**
 * Editor graph node for activating a questline graph asset at runtime. Designer authors:
 *   - Graph: the questline asset to activate.
 *   - Params: optional activation context applied to the questline's entry Step.
 *
 * Compiles to UStartQuestlineNode at runtime via FQuestlineGraphCompiler::CompileUtilityNodes.
 */
UCLASS()
class SIMPLEQUESTEDITOR_API UQuestlineNode_StartQuestline : public UQuestlineNode_UtilityBase
{
	GENERATED_BODY()

public:
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override
	{
		return NSLOCTEXT("SimpleQuestEditor", "StartQuestlineTitle", "Start Questline");
	}

	/** Questline graph asset to activate. Loaded asynchronously at runtime. */
	UPROPERTY(EditAnywhere, Category = "Start Questline")
	TSoftObjectPtr<UQuestlineGraph> Graph;

	/**
	 * Optional activation context applied to the questline's entry. Empty default activates with no extra
	 * context; populated Dynamic fields (Instigator, CustomData, lineage) layer onto the entry Step's
	 * authored defaults during activation.
	 */
	UPROPERTY(EditAnywhere, Category = "Start Questline")
	FQuestObjectiveActivationContext Params;
};