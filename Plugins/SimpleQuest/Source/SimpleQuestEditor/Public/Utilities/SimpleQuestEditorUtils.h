// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "Settings/SimpleQuestSettings.h"

// ---- Wire colors ----
#define SQ_ED_WIRE_ACTIVATION    (GetDefault<USimpleQuestSettings>()->ActivationWireColor)
#define SQ_ED_WIRE_PREREQUISITE  (GetDefault<USimpleQuestSettings>()->PrerequisiteWireColor)
#define SQ_ED_WIRE_OUTCOME       (GetDefault<USimpleQuestSettings>()->OutcomeWireColor)
#define SQ_ED_WIRE_DEACTIVATION  (GetDefault<USimpleQuestSettings>()->DeactivationWireColor)

// ---- Pin colors ----
#define SQ_ED_PIN_DEFAULT        (GetDefault<USimpleQuestSettings>()->DefaultPinColor)

// ---- Node title colors ----
#define SQ_ED_NODE_ENTRY         (GetDefault<USimpleQuestSettings>()->EntryNodeColor)
#define SQ_ED_NODE_QUEST         (GetDefault<USimpleQuestSettings>()->QuestNodeColor)
#define SQ_ED_NODE_STEP          (GetDefault<USimpleQuestSettings>()->StepNodeColor)
#define SQ_ED_NODE_LINKED        (GetDefault<USimpleQuestSettings>()->LinkedQuestlineGraphNodeColor)
#define SQ_ED_NODE_ACTIVATE_GROUP   (GetDefault<USimpleQuestSettings>()->ActivateGroupNodeColor)
#define SQ_ED_NODE_PREREQ_GROUP  (GetDefault<USimpleQuestSettings>()->PrerequisiteGroupNodeColor)
#define SQ_ED_NODE_UTILITY       (GetDefault<USimpleQuestSettings>()->UtilityNodeColor)
#define SQ_ED_NODE_GRAPH_OUTCOME (GetDefault<USimpleQuestSettings>()->GraphOutcomeNodeColor)


struct FConnectionParams;
struct FGraphPanelPinConnectionFactory;

namespace SimpleQuestEditorUtilities
{
	/**
	 * Sanitizes a designer-entered label into a valid Gameplay Tag segment. Trims whitespace, replaces any character that is not
	 * alphanumeric or underscore with an underscore.
	 */
	inline FString SanitizeQuestlineTagSegment(const FString& InLabel)
	{
		FString Result = InLabel.TrimStartAndEnd();
		for (TCHAR& Ch : Result)
		{
			if (!FChar::IsAlnum(Ch) && Ch != TEXT('_'))
				Ch = TEXT('_');
		}
		return Result;
	}
}