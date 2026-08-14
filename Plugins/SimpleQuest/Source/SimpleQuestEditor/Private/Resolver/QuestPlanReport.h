// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

// One text rendering of a plan, shared by every surface that reports one. Three existed before this: the console's
// itemised dump, the toolkit's hand-rolled one-liner with a different set of counts, and the table-export arm, which
// called the console's and then printed its own corrected version on the next line.

#include "CoreMinimal.h"
#include "Resolver/QuestInPlacePlan.h"


/**
 * WHAT the plan is about, which decides the vocabulary. Both directions share FQuestInPlacePlan - that reuse decision
 * held - but they do not share nouns: a questline plan describes nodes, the containers they sit in and the wires
 * between them, while a row plan describes rows in someone else's table and legitimately has no levels and no edges.
 * Rendering a row plan in questline vocabulary is what produced "graph ''" and a count of wire edges that can only
 * ever be zero.
 */
enum class EQuestPlanSubject : uint8
{
	Questline,
	Row,
};

/**
 * The headline counts, in the subject's own vocabulary and with no verb of its own - the CALLER supplies that, because
 * the caller is the only thing that knows whether it is planning, applying, importing or exporting. Separating the two
 * is what stopped an export from announcing itself as an import.
 */
FString BuildQuestPlanSummary(const FQuestInPlacePlan& Plan, EQuestPlanSubject Subject);

/**
 * Headline plus every itemised line. Prefix is the caller's verb, used verbatim - "ImportQuestline", "Build Plan".
 * Refusals and warnings go out at Warning so a filtered log still surfaces them.
 */
void LogQuestPlanReport(const FQuestInPlacePlan& Plan, EQuestPlanSubject Subject, const FString& Prefix);

