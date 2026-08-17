// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

// The in-place import pipeline as ONE callable operation, so the console command and the editor toolbar are both thin
// callers rather than one of them being the implementation. Nothing here logs or touches UI: outcomes come back as data
// and the caller decides how to say them, which is what lets a panel show what a log used to be the only witness to.

#include "CoreMinimal.h"
#include "QuestMappingSource.h"
#include "Resolver/QuestImportMapping.h"
#include "Resolver/QuestInPlacePlan.h"

class UQuestlineGraph;
class UQuestImportMapping;

struct FQuestImportRequest
{
	/** Where the data comes from - a folder read through a format provider, or a DataTable. */
	FQuestDataEndpoint Endpoint;

	/** Optional studio-shape translation. Absent means the source is already in our own shape. */
	const UQuestImportMapping* Mapping = nullptr;

	/** Resolved by the caller via QuestImport_ResolvePolicies, so it can report the mode before the run starts. */
	FQuestAbsentPolicyResolver Policies;

	/** Permit deletion of nodes the source no longer mentions. Ignored while planning; only an apply can delete. */
	bool bDeleteOrphans = false;
};

struct FQuestImportOutcome
{
	FQuestInPlacePlan Plan;
	FQuestApplyResult ApplyResult;    // only meaningful when the run was asked to apply
	
	/**
	 * Read/mapping warnings from a run that produced NO PLAN - an unreadable source, a refused mapping, a bad bundle.
	 * Once a plan exists they are moved onto it, because a warning about the read is a warning about the plan the read
	 * produced, and leaving them in two places asked every caller to merge two lists. None of them did.
	 * So: non-empty here means the run stopped early, and Error says why.
	 */
	TArray<FString> Warnings;

	/** Non-empty when the run stopped BEFORE producing a plan - an unreadable source, a refused mapping, a bad bundle. */
	FString Error;

	/** True when a plan was produced. An apply that was refused by the plan's own refusals still reports true here. */
	bool bPlanned = false;

	/** True when the run was asked to apply AND the plan permitted it. */
	bool bApplied = false;
};

/**
 * Read a source into a validated bundle. Shared by the in-place and fresh-create paths so the read pipeline - provider,
 * mapping, wire bindings, flow conventions, structural validation - has exactly one definition and cannot drift between
 * them. OutNodeRowsByKey holds pointers INTO OutBundle, so the two must outlive each other's use together.
 */
bool QuestImport_ReadAndValidate(const FQuestDataEndpoint& Endpoint, const UQuestImportMapping* Mapping,
	FQuestDataBundle& OutBundle, TMap<FString, const FQuestDataRow*>& OutNodeRowsByKey, TSet<FString>& OutAllRowKeys,
	TArray<FString>& OutWarnings, FString& OutError);

/**
 * The absent-cell policy a run will use. Resolved by the CALLER rather than inside the run, because the caller reports
 * which mode it is in before any work happens, and a plan is only interpretable against the policy that produced it.
 */
FQuestAbsentPolicyResolver QuestImport_ResolvePolicies(const UQuestImportMapping* Mapping, bool bResetAbsent);

/**
 * Read, plan against Target, and optionally apply - the whole in-place operation as one call. ALWAYS re-reads: apply
 * needs the bundle a plan describes, and a source can move between a plan being reviewed and being run, so re-planning
 * is the only honest way to promise that what was reviewed is what executes.
 * The caller owns the transaction; ApplyQuestPlan deliberately does not open one.
 */
bool QuestImport_RunInPlace(UQuestlineGraph& Target, const FQuestImportRequest& Request, bool bApply, FQuestImportOutcome& Out);