// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Resolver/QuestPlanReport.h"

#include "QuestExportOperations.h"
#include "SimpleQuestLog.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

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
		// MATCHED and CHANGED are separate numbers, the same way the questline summary reports them. Collapsing them
		// makes "every row matched and agreed" read identically to "nothing matched at all" - both print zero - and
		// those are the two outcomes a reviewer most needs to tell apart before writing into someone else's table.
		return FString::Printf(TEXT("%d row(s) to create, %d update(s) (%d with changes), %d refusal(s). %d row(s) there "
									"are claimed by nothing here and were left alone."),
			Plan.CountOf(EQuestNodePlanAction::Create),
			Plan.CountOf(EQuestNodePlanAction::Update),
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
	// A row plan names BOTH ends. The plan's one path is the QUESTLINE - the broker's key - and printing it alone beneath a
	// summary counting rows reads as though it were the table, which is the single fact a reviewer needs before allowing a
	// write into an asset somebody else owns. Falls back to the one path when a refusal fired before a destination was known.
	const FString PlanFor = (Subject == EQuestPlanSubject::Row && !Plan.DestinationAssetPath.IsEmpty())
		? FString::Printf(TEXT("'%s' writing into '%s'"), *Plan.TargetAssetPath, *Plan.DestinationAssetPath)
		: FString::Printf(TEXT("'%s'"), *Plan.TargetAssetPath);

	UE_LOG(LogSimpleQuestResolver, Log, TEXT("%s: plan for %s - %s"),
		*Prefix,
		*PlanFor,
		*BuildQuestPlanSummary(Plan, Subject));

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

FString BuildQuestExportReceipt(const FQuestExportOutcome& Outcome)
{
	// NOTHING WRITTEN IS NOT A FAILURE and should not read like one. "Exported 0 file(s)" sends someone hunting for a
	// problem when the honest answer is that there was nothing to do.
	if (Outcome.FilesWritten == 0)
	{
		return FString::Printf(TEXT("Nothing to write to '%s'%s"),
			*Outcome.OutDir,
			Outcome.bDestinationDerived ? TEXT(" (default destination)") : TEXT(""));
	}

	return FString::Printf(TEXT("Exported %d file(s) to '%s'%s"),
		Outcome.FilesWritten,
		*Outcome.OutDir,
		Outcome.bDestinationDerived ? TEXT(" (default destination)") : TEXT(""));
}

void LogQuestExportReport(const FQuestExportOutcome& Outcome, const FString& Prefix)
{
	// The knot count was in the console's line and absent from the toolkit's. Nobody decided that; the two were written
	// at different times and drifted, which is the whole reason this lives in one place now.
	UE_LOG(LogSimpleQuestResolver, Log, TEXT("%s: '%s' - %d entity row(s) across %d type(s), %d edge(s), %d knot(s) "
		"collapsed. %s; removed %d from the previous export."),
		*Prefix,
		*Outcome.ExportKey,
		Outcome.EntityRows,
		Outcome.TypeCount,
		Outcome.EdgeCount,
		Outcome.KnotsCollapsed,
		*BuildQuestExportReceipt(Outcome),
		Outcome.FilesRemoved);
}


namespace
{
	const TCHAR* PlanActionJson(EQuestNodePlanAction Action)
	{
		switch (Action)
		{
			case EQuestNodePlanAction::Create: return TEXT("create");
			case EQuestNodePlanAction::Orphan: return TEXT("orphan");
			default:                           return TEXT("update");
		}
	}

	const TCHAR* ChangeKindJson(EQuestPropertyChangeKind Kind)
	{
		switch (Kind)
		{
			case EQuestPropertyChangeKind::ChildAdded:   return TEXT("childAdded");
			case EQuestPropertyChangeKind::ChildRemoved: return TEXT("childRemoved");
			default:                                     return TEXT("edit");
		}
	}

	// Refused BEFORE clean, because IsNoOp() already folds in "no refusals" and would otherwise call a refused plan clean.
	// Derived here rather than by a caller so the artifact and the exit code cannot come apart.
	const TCHAR* PlanStatusJson(const FQuestInPlacePlan& Plan)
	{
		if (Plan.Refusals.Num() > 0) return TEXT("refused");
		return Plan.IsNoOp() ? TEXT("clean") : TEXT("differences");
	}

	void WriteSortedStrings(TJsonWriter<>& Writer, const TCHAR* Field, const TArray<FString>& Values)
	{
		TArray<FString> Sorted = Values;
		Sorted.Sort();
		Writer.WriteArrayStart(Field);
		for (const FString& Value : Sorted) { Writer.WriteValue(Value); }
		Writer.WriteArrayEnd();
	}

	void WriteEdges(TJsonWriter<>& Writer, const TCHAR* Field, const TArray<FQuestDataEdge>& Edges)
	{
		// The same ordering the TSV writer uses, so an edge reads identically wherever it is reported.
		TArray<FQuestDataEdge> Sorted = Edges;
		Sorted.Sort([](const FQuestDataEdge& A, const FQuestDataEdge& B)
		{
			if (A.From != B.From) return A.From < B.From;
			if (A.Type != B.Type) return A.Type < B.Type;
			return A.To < B.To;
		});
		Writer.WriteArrayStart(Field);
		for (const FQuestDataEdge& Edge : Sorted)
		{
			Writer.WriteObjectStart();
			Writer.WriteValue(TEXT("from"), Edge.From);
			Writer.WriteValue(TEXT("type"), Edge.Type);
			Writer.WriteValue(TEXT("to"),   Edge.To);
			Writer.WriteObjectEnd();
		}
		Writer.WriteArrayEnd();
	}

	void WriteChange(TJsonWriter<>& Writer, const FQuestPropertyChange& Change)
	{
		Writer.WriteObjectStart();
		Writer.WriteValue(TEXT("property"), Change.Property);
		Writer.WriteValue(TEXT("kind"),     ChangeKindJson(Change.Kind));

		// IncomingValue is deliberately absent: it exists so the APPLY step writes what planning decided, in-process. A
		// consumer of this file is not applying - it should re-run the plan rather than replay a serialized value.
		Writer.WriteObjectStart(TEXT("display"));
		Writer.WriteValue(TEXT("current"),  Change.CurrentText);
		Writer.WriteValue(TEXT("incoming"), Change.IncomingText);
		Writer.WriteObjectEnd();

		Writer.WriteObjectEnd();
	}

	void WriteEntry(TJsonWriter<>& Writer, const FQuestNodePlanEntry& Entry)
	{
		Writer.WriteObjectStart();

		// Everything OUTSIDE "display" is what a tool matches on. Empty strings are written rather than omitted so a
		// consumer diffing two runs never sees a key appear and disappear as entries change action.
		Writer.WriteValue(TEXT("key"),                Entry.Key);
		Writer.WriteValue(TEXT("action"),             PlanActionJson(Entry.Action));
		Writer.WriteValue(TEXT("guid"),               Entry.Guid);
		Writer.WriteValue(TEXT("class"),              Entry.ClassName);
		Writer.WriteValue(TEXT("currentClass"),       Entry.CurrentClassName);
		Writer.WriteValue(TEXT("graph"),              Entry.GraphCell);
		Writer.WriteValue(TEXT("currentGraph"),       Entry.CurrentGraphCell);
		Writer.WriteValue(TEXT("destinationRowName"), Entry.DestinationRowName.IsNone() ? FString() : Entry.DestinationRowName.ToString());
		Writer.WriteValue(TEXT("moved"),              Entry.bMoved);
		Writer.WriteValue(TEXT("isQuestlineSelf"),    Entry.bIsQuestlineSelf);

		TArray<FQuestPropertyChange> Changes = Entry.Changes;
		Changes.Sort([](const FQuestPropertyChange& A, const FQuestPropertyChange& B) { return A.Property < B.Property; });
		Writer.WriteArrayStart(TEXT("changes"));
		for (const FQuestPropertyChange& Change : Changes) { WriteChange(Writer, Change); }
		Writer.WriteArrayEnd();

		Writer.WriteObjectStart(TEXT("display"));
		Writer.WriteValue(TEXT("label"),        Entry.Label);
		Writer.WriteValue(TEXT("graph"),        Entry.GraphLabel);
		Writer.WriteValue(TEXT("currentGraph"), Entry.CurrentGraphLabel);
		Writer.WriteObjectEnd();

		Writer.WriteObjectEnd();
	}

	void WritePlan(TJsonWriter<>& Writer, const FQuestPlanRunItem& Item)
	{
		const FQuestInPlacePlan& Plan = Item.Plan;
		const bool bIntoTable = Plan.Direction == EQuestPlanDirection::IntoTable;

		Writer.WriteObjectStart();
		Writer.WriteValue(TEXT("questline"),   Plan.TargetAssetPath);
		Writer.WriteValue(TEXT("destination"), Plan.DestinationAssetPath);
		Writer.WriteValue(TEXT("direction"),   bIntoTable ? TEXT("intoTable") : TEXT("intoGraph"));
		Writer.WriteValue(TEXT("status"),      PlanStatusJson(Plan));

		Writer.WriteObjectStart(TEXT("source"));
		Writer.WriteValue(TEXT("folder"),  Item.Source.Folder);
		Writer.WriteValue(TEXT("format"),  Item.Source.Format);
		Writer.WriteValue(TEXT("mapping"), Item.Source.Mapping);
		Writer.WriteObjectEnd();

		// Every count, both directions, with the irrelevant one honestly at zero. untouchedNodes and unclaimedRows are
		// kept apart for the reason the struct keeps them apart: "outside every level the source declares" and "unclaimed
		// by any node" are different statements, and one name for both is how a reader conflates them.
		Writer.WriteObjectStart(TEXT("counts"));
		Writer.WriteValue(TEXT("create"),         Plan.CountOf(EQuestNodePlanAction::Create));
		Writer.WriteValue(TEXT("update"),         Plan.CountOf(EQuestNodePlanAction::Update));
		Writer.WriteValue(TEXT("orphan"),         Plan.CountOf(EQuestNodePlanAction::Orphan));
		Writer.WriteValue(TEXT("changed"),        Plan.ChangedNodeCount());
		Writer.WriteValue(TEXT("untouchedNodes"), Plan.UntouchedNodeCount);
		Writer.WriteValue(TEXT("unclaimedRows"),  Plan.UnclaimedRowCount);
		Writer.WriteValue(TEXT("ambiguousKeys"),  Plan.AmbiguousKeys.Num());
		Writer.WriteValue(TEXT("refusals"),       Plan.Refusals.Num());
		Writer.WriteValue(TEXT("edgesAdded"),     Plan.AddedEdges.Num());
		Writer.WriteValue(TEXT("edgesRemoved"),   Plan.RemovedEdges.Num());
		Writer.WriteObjectEnd();

		WriteSortedStrings(Writer, TEXT("refusals"),      Plan.Refusals);
		WriteSortedStrings(Writer, TEXT("warnings"),      Plan.Warnings);
		WriteSortedStrings(Writer, TEXT("ambiguousKeys"), Plan.AmbiguousKeys);

		TArray<FQuestNodePlanEntry> Entries = Plan.Entries;
		Entries.Sort([](const FQuestNodePlanEntry& A, const FQuestNodePlanEntry& B) { return A.Key < B.Key; });
		Writer.WriteArrayStart(TEXT("entries"));
		for (const FQuestNodePlanEntry& Entry : Entries) { WriteEntry(Writer, Entry); }
		Writer.WriteArrayEnd();

		Writer.WriteObjectStart(TEXT("edges"));
		WriteEdges(Writer, TEXT("added"),   Plan.AddedEdges);
		WriteEdges(Writer, TEXT("removed"), Plan.RemovedEdges);
		Writer.WriteObjectEnd();

		// LabelByKey is a TMap, and TMap iteration order is NOT stable between runs. Sorting the keys here is the whole
		// difference between an artifact a build server can diff and one that appears to change on every commit.
		TArray<FString> LabelKeys;
		Plan.LabelByKey.GetKeys(LabelKeys);
		LabelKeys.Sort();
		Writer.WriteObjectStart(TEXT("display"));
		Writer.WriteObjectStart(TEXT("labelByKey"));
		for (const FString& Key : LabelKeys) { Writer.WriteValue(Key, Plan.LabelByKey[Key]); }
		Writer.WriteObjectEnd();
		Writer.WriteObjectEnd();

		Writer.WriteObjectEnd();
	}
}

FString BuildQuestPlanRunJson(const TArray<FQuestPlanRunItem>& Items, const FString& Root, const FString& FailOn)
{
	TArray<FQuestPlanRunItem> Sorted = Items;
	Sorted.Sort([](const FQuestPlanRunItem& A, const FQuestPlanRunItem& B)
	{
		// By questline, then by folder: one questline can legitimately be described by more than one corpus folder, and
		// leaving those two in discovery order would make the file depend on directory enumeration.
		if (A.Plan.TargetAssetPath != B.Plan.TargetAssetPath) return A.Plan.TargetAssetPath < B.Plan.TargetAssetPath;
		return A.Source.Folder < B.Source.Folder;
	});
	
	int32 CleanCount = 0, DifferenceCount = 0, RefusedCount = 0;
	for (const FQuestPlanRunItem& Item : Sorted)
	{
		const FString Status = PlanStatusJson(Item.Plan);
		if (Status == TEXT("refused"))          { ++RefusedCount; }
		else if (Status == TEXT("differences")) { ++DifferenceCount; }
		else                                    { ++CleanCount; }
	}

	FString Json;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
	Writer->WriteObjectStart();

	// The version is FIRST and is a promise. A consumer that finds a number it does not know should refuse rather than
	// guess, which is only possible if it can read the number before anything else it might misinterpret.
	Writer->WriteValue(TEXT("schemaVersion"), 1);
	Writer->WriteValue(TEXT("root"),          Root);
	Writer->WriteValue(TEXT("failOn"),        FailOn);

	Writer->WriteObjectStart(TEXT("summary"));
	Writer->WriteValue(TEXT("planCount"),       Sorted.Num());
	Writer->WriteValue(TEXT("clean"),           CleanCount);
	Writer->WriteValue(TEXT("withDifferences"), DifferenceCount);
	Writer->WriteValue(TEXT("refused"),         RefusedCount);
	Writer->WriteObjectEnd();

	Writer->WriteArrayStart(TEXT("plans"));
	for (const FQuestPlanRunItem& Item : Sorted) { WritePlan(Writer.Get(), Item); }
	Writer->WriteArrayEnd();

	Writer->WriteObjectEnd();
	Writer->Close();
	return Json;
}

