// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Resolver/QuestRowApply.h"

#include "DataTableEditorUtils.h"
#include "Engine/DataTable.h"
#include "Resolver/QuestDataBundle.h"
#include "Resolver/QuestRowRestore.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"


void ApplyQuestRowPlan(UDataTable& Destination, const FQuestInPlacePlan& Plan,
					   const TMap<FString, const FQuestDataRow*>& RowsByKey, FQuestApplyResult& OutResult)
{
	// The same wholesale refusal the graph direction makes, for the same reason: refusals or contested keys say the
	// planner could not describe the write, not that one row is unusual. Writing the rest would act on a description
	// already known to be incomplete - into an asset we do not own.
	if (!Plan.Refusals.IsEmpty() || !Plan.AmbiguousKeys.IsEmpty())
	{
		OutResult.bRefused = true;
		return;
	}

	const UScriptStruct* RowStruct = Destination.GetRowStruct();
	if (!RowStruct)
	{
		OutResult.bRefused = true;
		return;
	}

	// MODIFY BEFORE MUTATING. The transaction buffer snapshots the object at the moment Modify is called, so a Modify
	// AFTER a write records the already-changed state and undo restores nothing - which is exactly the defect in
	// ApplyTagRenamesToLoadedAssets. Modify(false) because we do not yet know whether anything will change; the caller
	// dirties once at the end, only if something did.
	// A COST WORTH KNOWING: Modify on a UDataTable serializes the ENTIRE row map into the transaction buffer, so one
	// undo step costs a full table copy however few rows change. Called ONCE here rather than per row for that reason.
	Destination.Modify(false);

	const TMap<FName, uint8*>& Rows = Destination.GetRowMap();
	bool bChanged = false;

	for (const FQuestNodePlanEntry& Entry : Plan.Entries)
	{
		const FName RowName(*Entry.Key);

		if (Entry.Action == EQuestNodePlanAction::Create)
		{
			const FQuestDataRow* const* Incoming = RowsByKey.Find(Entry.Key);
			if (!Incoming || !*Incoming)
			{
				OutResult.Skipped.Add(FString::Printf(TEXT("'%s' was planned as a new row, but its source row is gone"), *Entry.Key));
				continue;
			}

			// Seeded at the struct's DEFAULTS and then written into, so a field the source never mentions holds that
			// struct's default rather than whatever the allocation happened to contain.
			FStructOnScope NewRow(RowStruct);
			int32 Written = 0;
			for (const TPair<FString, FQuestDataValue>& Cell : (*Incoming)->Cells)
			{
				if (Cell.Key == TEXT("class") || Cell.Key == TEXT("graph")) continue;   // structural, never a field
				FProperty* Prop = RowStruct->FindPropertyByName(FName(*Cell.Key));
				if (!Prop) continue;   // the planner already refused or warned about this column, once for the table
				if (RestoreQuestCell(Prop, Prop->ContainerPtrToValuePtr<void>(NewRow.GetStructMemory()), Cell.Value))
				{
					++Written;
				}
			}

			Destination.AddRow(RowName, NewRow.GetStructMemory(), RowStruct);
			OutResult.PropertiesWritten += Written;
			++OutResult.EntitiesCreated;
			bChanged = true;
			continue;
		}

		if (Entry.Changes.IsEmpty()) { continue; }   // an update that already agrees is not work

		uint8* const* Existing = Rows.Find(RowName);
		if (!Existing || !*Existing)
		{
			OutResult.Skipped.Add(FString::Printf(TEXT("'%s' was planned as an update, but that row is no longer there"), *Entry.Key));
			continue;
		}

		for (const FQuestPropertyChange& Change : Entry.Changes)
		{
			FProperty* Prop = RowStruct->FindPropertyByName(FName(*Change.Property));
			if (!Prop)
			{
				OutResult.Skipped.Add(FString::Printf(TEXT("'%s' resolves to no field on '%s'"),
					*Change.Property, *RowStruct->GetName()));
				continue;
			}

			// The value the PLAN computed, never a re-reading of the row - the same rule the graph direction follows,
			// because "what the source says" and "what the field would end up holding" are different questions.
			if (!RestoreQuestCell(Prop, Prop->ContainerPtrToValuePtr<void>(*Existing), Change.IncomingValue))
			{
				OutResult.Skipped.Add(FString::Printf(TEXT("'%s' could not be written to row '%s' - the value does not fit "
					"that field"), *Change.Property, *Entry.Key));
				continue;
			}
			++OutResult.PropertiesWritten;
			bChanged = true;
		}
	}

	// Two-stage notification - the same pair ApplyTagRenamesToObject uses, and for the reason recorded there:
	// HandleDataTableChanged reaches consumers watching the asset, BroadcastPostChange reaches the Data Table editor's
	// ROW GRID, and without the second the detail view refreshes while the grid stays stale until close and reopen.
	if (bChanged)
	{
		Destination.HandleDataTableChanged(NAME_None);
		FDataTableEditorUtils::BroadcastPostChange(&Destination, FDataTableEditorUtils::EDataTableChangeInfo::RowList);
	}
}

