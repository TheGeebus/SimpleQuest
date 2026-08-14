// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Resolver/QuestPlanReport.h"

#include "SimpleQuestLog.h"

namespace
{
	const TCHAR* PlanActionName(EQuestNodePlanAction Action)
	{
		switch (Action)
		{
			case EQuestNodePlanAction::Create: return TEXT("CREATE");
			case EQuestNodePlanAction::Update: return TEXT("UPDATE");
			case EQuestNodePlanAction::Orphan: return TEXT("ORPHAN");
			default:                           return TEXT("?");
		}
	}
}

FString BuildQuestPlanSummary(const FQuestInPlacePlan& Plan, EQuestPlanSubject Subject)
{
	if (Subject == EQuestPlanSubject::Row)
	{
		// A row plan has no orphans, no moves and no wires - it can only add or change rows, and anything in that table
		// this questline does not claim is deliberately left alone rather than treated as debris.
		return FString::Printf(TEXT("%d row(s) to create, %d to update, %d refusal(s). %d row(s) there are claimed by "
								    "nothing here and were left alone."),
			Plan.CountOf(EQuestNodePlanAction::Create),
			Plan.ChangedNodeCount(),
			Plan.Refusals.Num(),
			Plan.UnclaimedRowCount);
	}

	return FString::Printf(TEXT("%d update(s) (%d with changes), %d create(s), %d orphan(s), %d node(s) outside the "
								"levels this source declares, %d contested key(s), %d wire edge(s) added, %d removed, "
								"%d refusal(s)."),
		Plan.CountOf(EQuestNodePlanAction::Update),
		Plan.ChangedNodeCount(),
		Plan.CountOf(EQuestNodePlanAction::Create),
		Plan.CountOf(EQuestNodePlanAction::Orphan),
		Plan.UntouchedNodeCount,
		Plan.AmbiguousKeys.Num(),
		Plan.AddedEdges.Num(),
		Plan.RemovedEdges.Num(),
		Plan.Refusals.Num());
}

void LogQuestPlanReport(const FQuestInPlacePlan& Plan, EQuestPlanSubject Subject, const FString& Prefix)
{
	UE_LOG(LogSimpleQuestResolver, Log, TEXT("%s: plan for '%s' - %s"),
		   *Prefix, *Plan.TargetAssetPath, *BuildQuestPlanSummary(Plan, Subject));

	// Wires are questline-only. Looping an empty array would be harmless but the SILENCE is the point: a row plan that
	// printed a wire section with nothing under it reads as "no wires changed" rather than "wires are not a thing here".
	if (Subject == EQuestPlanSubject::Questline)
	{
		for (const FQuestDataEdge& E : Plan.RemovedEdges) { UE_LOG(LogSimpleQuestResolver, Log, TEXT("  [WIRE-] %s|%s|%s"), *E.From, *E.Type, *E.To); }
		for (const FQuestDataEdge& E : Plan.AddedEdges)   { UE_LOG(LogSimpleQuestResolver, Log, TEXT("  [WIRE+] %s|%s|%s"), *E.From, *E.Type, *E.To); }
	}

	for (const FString& R : Plan.Refusals) { UE_LOG(LogSimpleQuestResolver, Warning, TEXT("  [REFUSED] %s"), *R); }

	for (const FQuestNodePlanEntry& Entry : Plan.Entries)
	{
		// Unchanged matches are the common case on a healthy re-import; listing them would bury the ones that matter.
		if (Entry.Action == EQuestNodePlanAction::Update && Entry.Changes.Num() == 0 && !Entry.bMoved) continue;

		// An orphan has no incoming row, so its level is only known from the asset side; the questline itself sits in no
		// level at all, being the thing levels belong to; and a ROW sits in no level either, which is a fact about the
		// destination rather than a missing value - hence omitted rather than printed empty.
		const FString& Level = (Entry.Action == EQuestNodePlanAction::Orphan) ? Entry.CurrentGraphCell : Entry.GraphCell;
		FString Where;
		if (Entry.bIsQuestlineSelf)                                   { Where = TEXT(" - the questline itself"); }
		else if (Subject == EQuestPlanSubject::Questline && !Level.IsEmpty()) { Where = FString::Printf(TEXT(" - graph '%s'"), *Level); }

		UE_LOG(LogSimpleQuestResolver, Log, TEXT("  [%s] %s (%s)%s%s"),
			PlanActionName(Entry.Action),
			*Entry.Key,
			*Entry.ClassName,
			*Where,
			Entry.bMoved ? TEXT("  ** moves to a different container **") : TEXT(""));

		if (Entry.bMoved)
		{
			UE_LOG(LogSimpleQuestResolver, Log, TEXT("      graph: %s -> %s"), *Entry.CurrentGraphCell, *Entry.GraphCell);
		}
		for (const FQuestPropertyChange& Change : Entry.Changes)
		{
			UE_LOG(LogSimpleQuestResolver, Log, TEXT("      %s: '%s' -> '%s'"), *Change.Property, *Change.CurrentText, *Change.IncomingText);
		}
	}

	for (const FString& W : Plan.Warnings) { UE_LOG(LogSimpleQuestResolver, Warning, TEXT("%s: %s"), *Prefix, *W); }
}

