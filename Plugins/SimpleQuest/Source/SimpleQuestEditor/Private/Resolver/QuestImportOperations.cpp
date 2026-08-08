// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "QuestImportOperations.h"

#include "QuestBundleTransforms.h"
#include "QuestFlowConventions.h"
#include "QuestInPlaceApply.h"
#include "QuestInPlacePlanner.h"
#include "Resolver/QuestDataBundle.h"
#include "Resolver/QuestImportMapping.h"

bool QuestImport_ReadAndValidate(const FQuestDataEndpoint& Endpoint, const UQuestImportMapping* Mapping,
	FQuestDataBundle& OutBundle, TMap<FString, const FQuestDataRow*>& OutNodeRowsByKey, TSet<FString>& OutAllRowKeys,
	TArray<FString>& OutWarnings, FString& OutError)
{
	if (!ReadEndpointBundle(Endpoint, OutBundle, OutError)) { return false; }

	// A studio's source-shape translation, before the flow conventions so a mapped column can feed one.
	if (Mapping)
	{
		if (!QuestBundle_ApplyMapping(OutBundle, *Mapping, OutWarnings))
		{
			// Phrased as the caller will print it. The three failure modes here produce three different messages today,
			// and collapsing them into one call must not collapse the messages - a reader distinguishes "could not read"
			// from "refused the shape" from "read but incoherent" by exactly this wording.
			OutError = TEXT("mapping guard refused the import");
			return false;
		}
		QuestBundle_ApplyWireBindings(OutBundle, *Mapping, OutWarnings);
	}
	ApplyQuestFlowConventions(OutBundle, OutWarnings);

	FString ValidateError;
	if (!QuestBundle_Validate(OutBundle, OutNodeRowsByKey, OutAllRowKeys, ValidateError))
	{
		OutError = FString::Printf(TEXT("validation failed - %s"), *ValidateError);
		return false;
	}
	return true;
}

FQuestAbsentPolicyResolver QuestImport_ResolvePolicies(const UQuestImportMapping* Mapping, bool bResetAbsent)
{
	FQuestAbsentPolicyResolver Policies;
	if (Mapping)
	{
		Policies.Default = Mapping->DefaultAbsentPolicy;
		for (const FQuestColumnBinding& B : Mapping->Bindings)
		{
			if (!B.TargetProperty.IsNone()) { Policies.ByProperty.Add(B.TargetProperty, B.AbsentPolicy); }
		}
	}
	// A RUN-LEVEL instruction has to reach the per-property entries, not just the fallback: a recipe writes an explicit
	// entry per bound column, and a per-binding Preserve would otherwise shadow the flag for exactly the columns the
	// recipe covers. Require is left alone - that is a designer asserting a value must be present.
	if (bResetAbsent)
	{
		if (Policies.Default == EQuestAbsentFieldPolicy::Preserve) { Policies.Default = EQuestAbsentFieldPolicy::Reset; }
		for (TPair<FName, EQuestAbsentFieldPolicy>& Entry : Policies.ByProperty)
		{
			if (Entry.Value == EQuestAbsentFieldPolicy::Preserve) { Entry.Value = EQuestAbsentFieldPolicy::Reset; }
		}
	}
	return Policies;
}

bool QuestImport_RunInPlace(UQuestlineGraph& Target, const FQuestImportRequest& Request, bool bApply, FQuestImportOutcome& Out)
{
	FQuestDataBundle Bundle;
	TMap<FString, const FQuestDataRow*> NodeRowsByKey;
	TSet<FString> AllRowKeys;
	if (!QuestImport_ReadAndValidate(Request.Endpoint, Request.Mapping, Bundle, NodeRowsByKey, AllRowKeys, Out.Warnings, Out.Error))
	{
		return false;
	}

	PlanQuestInPlace(Target, Bundle, NodeRowsByKey, Out.Warnings, Out.Plan, Request.Policies);
	Out.bPlanned = true;

	if (!bApply) { return true; }

	FQuestApplyOptions Options;
	if (Request.Mapping) { Options.bDeleteOrphanedNodes = Request.Mapping->bDeleteOrphanedNodes; }
	if (Request.bDeleteOrphans) { Options.bDeleteOrphanedNodes = true; }

	ApplyQuestPlan(Target, Out.Plan, Bundle, NodeRowsByKey, Out.ApplyResult, Options);
	Out.bApplied = !Out.ApplyResult.bRefused;
	return true;
}

