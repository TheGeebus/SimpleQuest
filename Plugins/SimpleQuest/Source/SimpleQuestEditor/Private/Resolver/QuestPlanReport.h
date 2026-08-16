// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

// Every rendering of a plan, shared by every surface that reports one. Three text renderings existed before this: the
// console's itemised dump, the toolkit's hand-rolled one-liner with a different set of counts, and the table-export arm,
// which called the console's and then printed its own corrected version on the next line.
//
// TWO AUDIENCES NOW, one model. The log renderings are read by a person, so they may reword freely. The JSON rendering is
// read by a build server and is a CONTRACT: its field names and its ordering are promises, and it carries a schema
// version because a gate that breaks silently when the shape moves is worse than no gate at all.

#include "CoreMinimal.h"
#include "Resolver/QuestInPlacePlan.h"


struct FQuestExportOutcome;


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

/**
 * THE RECEIPT: what was written and where, in one sentence. Short on purpose - it goes on a panel beside the plan
 * rather than into a log, and a designer reads it to confirm the thing they just asked for actually happened.
 */
FString BuildQuestExportReceipt(const FQuestExportOutcome& Outcome);

/** The full detail line for a log, receipt included. Prefix is the caller's verb, same contract as the plan report. */
void LogQuestExportReport(const FQuestExportOutcome& Outcome, const FString& Prefix);


/** Where one plan in a run came from - the export marker's account of its folder, as the run should report it. */
struct FQuestPlanRunSource
{
	FString Folder;    // RELATIVE to the run's root, so two machines planning the same corpus produce the same file
	FString Format;
	FString Mapping;   // empty for a canonical export
	int32 RowCount = 0;   // how much data the corpus holds - a fact about the SOURCE, not about any comparison
};

/** One plan plus the provenance a consumer needs to act on it. */
struct FQuestPlanRunItem
{
	/**
	 * The questline this corpus CLAIMS, taken from its marker - not from Plan.TargetAssetPath. A corpus whose asset does
	 * not exist yet has no plan and therefore no such field, and switching the report's source depending on whether
	 * planning happened is precisely how one report comes to name two different things. One field, always populated.
	 */
	FString Questline;

	/**
	 * False when the source was read and validated but never compared, because there was no asset to compare against.
	 * That is a legitimate, common outcome - progression data authored before anyone builds the questline - and it is
	 * NOT the same as "the asset matches", which is why it gets its own status rather than an empty plan.
	 * Defaults to false so a caller that forgets to set it reports every corpus as unplanned, which is loud and gets
	 * fixed on the first run; the opposite default would silently report a missing asset as clean.
	 */
	bool bPlanned = false;

	/**
	 * Empty apart from Refusals when bPlanned is false. A read failure goes in Plan.Refusals deliberately: a refusal is
	 * a refusal whether it was earned by reading the source or by comparing it, and one channel is what lets a consumer
	 * ask "what is wrong here" without first asking how far the run got.
	 */
	FQuestInPlacePlan Plan;

	FQuestPlanRunSource Source;
};

/**
 * The whole run as JSON - the machine-readable half of the plan report, and the only one that is a contract.
 *
 * DETERMINISTIC BY CONSTRUCTION: identical inputs produce a BYTE-IDENTICAL string. Every collection is sorted here
 * rather than trusted to arrive ordered, and nothing that varies between runs goes in - no timestamp, no duration, no
 * absolute path. That is not tidiness: the point of the artifact is that a build server can diff this commit's run
 * against the last one, and a single moving field turns every diff into noise.
 *
 * The subject is DERIVED from each plan's Direction rather than passed in, so a caller cannot hand this a row plan
 * labelled as a questline - which is exactly how a row plan came to name the wrong asset in the log.
 */
FString BuildQuestPlanRunJson(const TArray<FQuestPlanRunItem>& Items, const FString& Root, const FString& FailOn);

