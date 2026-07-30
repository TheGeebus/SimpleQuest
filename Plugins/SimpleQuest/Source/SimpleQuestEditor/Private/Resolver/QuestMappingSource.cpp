// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Resolver/QuestMappingSource.h"

#include "Resolver/QuestDataFormatRegistry.h"
#include "Resolver/ISimpleQuestDataFormat.h"
#include "Resolver/QuestDataBundle.h"
#include "Engine/DataTable.h"
#include "SimpleQuestLog.h"
#include "Resolver/QuestImportMapping.h"

#define LOCTEXT_NAMESPACE "SimpleQuestMappingSource"

FQuestSourceColumns EnumerateForeignFileColumns(const FString& FormatName, const FString& SourceFolder)
{
	FQuestSourceColumns Result;

	if (FormatName.IsEmpty() || SourceFolder.IsEmpty())
	{
		Result.Error = LOCTEXT("NoDescriptor", "Set a source format and folder to list columns.");
		return Result;
	}

	const TUniquePtr<ISimpleQuestDataFormat> Format = FQuestDataFormatRegistry::Get().Create(FormatName);
	if (!Format)
	{
		Result.Error = FText::Format(LOCTEXT("BadFormat", "Format '{0}' is not registered."), FText::FromString(FormatName));
		return Result;
	}

	FQuestDataBundle Bundle;
	if (!Format->ReadBundle(SourceFolder, Bundle))
	{
		Result.Error = FText::Format(LOCTEXT("Unreadable", "Couldn't read source at '{0}' as {1}."),
			FText::FromString(SourceFolder), FText::FromString(Format->FormatName()));
		return Result;
	}

	// The source is readable. Collect the UNION of value columns across every content table (all node rows share one
	// discriminator + one binding set, so a column that appears in ANY content table is bindable). Exclude the self-row
	// table — it isn't fanned-out source content. Detect a within-table duplicate: the parse already keyed cells by column
	// name (collapsing a repeated header), so a duplicate means the data is irrecoverably ambiguous — a blocking error.
	TSet<FName> Seen;
	for (const TPair<FString, FQuestDataTable>& TablePair : Bundle.TablesByType)
	{
		if (TablePair.Key == TEXT("questline_graph")) continue;   // self row, not fanned source content

		TSet<FName> WithinTable;
		for (const FString& Col : TablePair.Value.Columns)
		{
			const FName ColName(*Col);
			if (WithinTable.Contains(ColName))
			{
				Result.bHasDuplicateColumns = true;
				Result.Error = FText::Format(LOCTEXT("DupColumn", "Source table '{0}' has a duplicate column '{1}' — its data is ambiguous."),
					FText::FromString(TablePair.Key), FText::FromName(ColName));
				return Result;   // refuse the whole source; a duplicate is a hard source-validity error
			}
			WithinTable.Add(ColName);
			Seen.Add(ColName);
		}
	}

	Result.Columns = Seen.Array();
	Result.Columns.Sort(FNameLexicalLess());   // stable, alphabetical for the dropdown
	Result.bReadable = true;
	return Result;
}

FQuestSourceColumns EnumerateDataTableColumns(const TSoftObjectPtr<UDataTable>& SourceTable)
{
	FQuestSourceColumns Result;

	const UDataTable* Table = SourceTable.LoadSynchronous();
	if (!Table)
	{
		Result.Error = LOCTEXT("NoTable", "Set a Data Table to list columns.");
		return Result;
	}
	const UScriptStruct* RowStruct = Table->GetRowStruct();
	if (!RowStruct)
	{
		Result.Error = LOCTEXT("NoRowStruct", "The Data Table has no row struct.");
		return Result;
	}

	// Walk the row struct's properties. Field names are unique by construction (a UScriptStruct can't hold two members of
	// the same name), so no duplicate check is needed — this provenance is structurally unambiguous.
	for (TFieldIterator<FProperty> It(RowStruct); It; ++It)
	{
		Result.Columns.Add(It->GetFName());
	}
	Result.Columns.Sort(FNameLexicalLess());
	Result.bReadable = true;
	return Result;
}

FQuestSourceColumns EnumerateMappingSourceColumns(const UQuestImportMapping& Mapping)
{
	switch (Mapping.SourceKind)
	{
	case EQuestMappingSourceKind::DataTable:
		return EnumerateDataTableColumns(Mapping.SourceDataTable);
	case EQuestMappingSourceKind::ForeignFile:
	default:
		return EnumerateForeignFileColumns(Mapping.SourceFormatName.ToString(), Mapping.SourceFolder);
	}
}

FString NormalizeDiscriminatorValue(const FString& Raw)
{
	return Raw.TrimStartAndEnd().ToLower();
}

bool BuildDiscriminatorClassMap(const UQuestImportMapping& Mapping, TMap<FString, UClass*>& OutClassByNormValue,
                                TArray<FText>& OutErrors)
{
	const int32 ErrorsBefore = OutErrors.Num();
	TMap<FString, FString> NormToRaw;   // normalized -> the raw key that first claimed it, for collision reporting

	for (const TPair<FString, TSoftClassPtr<UQuestlineNodeBase>>& Pair : Mapping.ClassByDiscriminatorValue)
	{
		const FString Norm = NormalizeDiscriminatorValue(Pair.Key);

		if (const FString* Prior = NormToRaw.Find(Norm))
		{
			OutErrors.Add(FText::Format(
				LOCTEXT("NormKeyCollision", "Discriminator values '{0}' and '{1}' are the same after normalization — one would silently override the other."),
				FText::FromString(*Prior), FText::FromString(Pair.Key)));
			continue;
		}
		NormToRaw.Add(Norm, Pair.Key);

		UClass* Cls = Pair.Value.LoadSynchronous();
		if (!Cls)
		{
			OutErrors.Add(FText::Format(
				LOCTEXT("UnresolvableClass", "Discriminator value '{0}' maps to a class that can't be resolved."),
				FText::FromString(Pair.Key)));
			continue;
		}
		OutClassByNormValue.Add(Norm, Cls);
	}
	return OutErrors.Num() == ErrorsBefore;
}

bool ValidateMappingAgainstSource(const UQuestImportMapping& Mapping, const TArray<FName>& ActualColumns,
                                  const TArray<FString>& ActualDiscriminatorValues, TArray<FText>& OutErrors)
{
	const int32 ErrorsBefore = OutErrors.Num();

	// The class map: catches unresolvable-class + normalized-key-collision, and gives the accepted normalized-value set
	// that EXACTLY matches what ApplyMapping will route (same builder, no drift). Its errors accumulate into OutErrors.
	TMap<FString, UClass*> ClassByNormValue;
	BuildDiscriminatorClassMap(Mapping, ClassByNormValue, OutErrors);

	// (i) + (v): per-binding column existence + the None+Require contradiction.
	const TSet<FName> ColumnSet(ActualColumns);
	for (const FQuestColumnBinding& B : Mapping.Bindings)
	{
		if (B.SourceColumn.IsNone())
		{
			if (B.AbsentPolicy == EQuestAbsentFieldPolicy::Require)
			{
				OutErrors.Add(FText::Format(
					LOCTEXT("NoneRequire", "Binding to '{0}' is unmapped (None) but its policy is Require — a value can't be required from a column that isn't mapped."),
					FText::FromName(B.TargetProperty)));
			}
			continue;
		}
		if (!ColumnSet.Contains(B.SourceColumn))
		{
			OutErrors.Add(FText::Format(
				LOCTEXT("MissingColumn", "Binding source column '{0}' (to {1}) is not present in the source."),
				FText::FromName(B.SourceColumn), FText::FromName(B.TargetProperty)));
		}
	}

	// (ii) + (iv): every ACTUAL discriminator value must resolve against the accepted (resolvable, collision-free) set.
	for (const FString& ActualValue : ActualDiscriminatorValues)
	{
		if (!ClassByNormValue.Contains(NormalizeDiscriminatorValue(ActualValue)))
		{
			OutErrors.Add(FText::Format(
				LOCTEXT("UnmappedValue", "Discriminator value '{0}' appears in the source but has no class mapping — its rows would be dropped."),
				FText::FromString(ActualValue)));
		}
	}

	return OutErrors.Num() == ErrorsBefore;
}

#undef LOCTEXT_NAMESPACE
