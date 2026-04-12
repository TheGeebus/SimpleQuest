// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "Settings/SimpleQuestSettings.h"

// ---- Wire colors ----
#define SQ_ED_WIRE_ACTIVATION		(GetDefault<USimpleQuestSettings>()->ActivationWireColor)
#define SQ_ED_WIRE_PREREQUISITE		(GetDefault<USimpleQuestSettings>()->PrerequisiteWireColor)
#define SQ_ED_WIRE_OUTCOME			(GetDefault<USimpleQuestSettings>()->OutcomeWireColor)
#define SQ_ED_WIRE_DEACTIVATION		(GetDefault<USimpleQuestSettings>()->DeactivationWireColor)

// ---- Pin colors ----
#define SQ_ED_PIN_DEFAULT			(GetDefault<USimpleQuestSettings>()->DefaultPinColor)

// ---- Node title colors ----
#define SQ_ED_NODE_ENTRY			(GetDefault<USimpleQuestSettings>()->EntryNodeColor)
#define SQ_ED_NODE_EXIT_ACTIVE		(GetDefault<USimpleQuestSettings>()->ExitNodeActiveColor)
#define SQ_ED_NODE_EXIT_INACTIVE	(GetDefault<USimpleQuestSettings>()->ExitNodeInactiveColor)
#define SQ_ED_NODE_QUEST			(GetDefault<USimpleQuestSettings>()->QuestNodeColor)
#define SQ_ED_NODE_STEP				(GetDefault<USimpleQuestSettings>()->StepNodeColor)
#define SQ_ED_NODE_LINKED			(GetDefault<USimpleQuestSettings>()->LinkedQuestlineGraphNodeColor)
#define SQ_ED_NODE_ACTIVATE_GROUP   (GetDefault<USimpleQuestSettings>()->ActivateGroupNodeColor)
#define SQ_ED_NODE_PREREQ_GROUP		(GetDefault<USimpleQuestSettings>()->PrerequisiteGroupNodeColor)
#define SQ_ED_NODE_UTILITY			(GetDefault<USimpleQuestSettings>()->UtilityNodeColor)
#define SQ_ED_NODE_GRAPH_OUTCOME	(GetDefault<USimpleQuestSettings>()->GraphOutcomeNodeColor)


struct FGameplayTag;
class UQuestObjective;
class UK2Node_CompleteObjectiveWithOutcome; 
struct FConnectionParams;
struct FGraphPanelPinConnectionFactory;

namespace USimpleQuestEditorUtilities
{
	/**
	 * Sanitizes a designer-entered label into a valid Gameplay Tag segment. Trims whitespace, replaces any character that is not
	 * alphanumeric or underscore with an underscore.
	 */
	FString SanitizeQuestlineTagSegment(const FString& InLabel);


	/**
	 * Collects unique OutcomeTags from all Exit nodes in a graph. Returns the tag names suitable for passing directly to SyncPinsByCategory.
	 */
	TArray<FName> CollectExitOutcomeTagNames(const UEdGraph* Graph);

	/**
	 * Discovers possible outcome tags for an objective class. Scans the class's Blueprint
	 * graphs for UK2Node_CompleteObjectiveWithOutcome instances; falls back to the CDO's
	 * PossibleOutcomes array for C++ classes or Blueprints without completion nodes.
	 */
	TArray<FGameplayTag> DiscoverObjectiveOutcomes(TSubclassOf<UQuestObjective> ObjectiveClass);
}