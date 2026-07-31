// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Resolver/QuestMappingSource.h"

#include "Resolver/QuestDataFormatRegistry.h"
#include "Resolver/ISimpleQuestDataFormat.h"
#include "Resolver/QuestDataBundle.h"
#include "Engine/DataTable.h"
#include "SimpleQuestLog.h"
#include "Resolver/QuestImportMapping.h"
#include "Settings/SimpleQuestSettings.h"

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

TArray<FString> EnumerateColumnDistinctValues(const FString& FormatName, const FString& SourceFolder, const FName& ColumnName)
{
	TArray<FString> Distinct;
	if (FormatName.IsEmpty() || SourceFolder.IsEmpty() || ColumnName.IsNone()) return Distinct;

	const TUniquePtr<ISimpleQuestDataFormat> Format = FQuestDataFormatRegistry::Get().Create(FormatName);
	if (!Format) return Distinct;

	FQuestDataBundle Bundle;
	if (!Format->ReadBundle(SourceFolder, Bundle)) return Distinct;

	// Collect distinct values of the column across every content table (same self-row exclusion as the column enumerator).
	// First-seen order keeps the row list stable as the designer works; a Set guards the "seen" test without reordering.
	const FString ColKey = ColumnName.ToString();
	TSet<FString> Seen;
	for (const TPair<FString, FQuestDataTable>& TablePair : Bundle.TablesByType)
	{
		if (TablePair.Key == TEXT("questline_graph")) continue;   // self row, not fanned source content
		for (const FQuestDataRow& Row : TablePair.Value.Rows)
		{
			const FString Value = Row.Get(ColKey);
			if (Value.IsEmpty()) continue;               // an absent/empty discriminator cell names no kind — skip it
			if (Seen.Contains(Value)) continue;
			Seen.Add(Value);
			Distinct.Add(Value);
		}
	}
	return Distinct;
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
								  const TArray<FString>& ActualDiscriminatorValues, TArray<FText>& OutErrors,
								  TArray<FText>* OutWarnings)
{
	const int32 ErrorsBefore = OutErrors.Num();

	// The class map: catches unresolvable-class + normalized-key-collision, and gives the accepted normalized-value set
	// that EXACTLY matches what ApplyMapping will route (same builder, no drift). Its errors accumulate into OutErrors.
	TMap<FString, UClass*> ClassByNormValue;
	BuildDiscriminatorClassMap(Mapping, ClassByNormValue, OutErrors);

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

	for (const FString& ActualValue : ActualDiscriminatorValues)
	{
		if (!ClassByNormValue.Contains(NormalizeDiscriminatorValue(ActualValue)))
		{
			OutErrors.Add(FText::Format(
				LOCTEXT("UnmappedValue", "Discriminator value '{0}' appears in the source but has no class mapping — its rows would be dropped."),
				FText::FromString(ActualValue)));
		}
	}

	// Advisory (non-fatal): the discriminator column is also bound to a property. Legal — the value populates the property AND
	// still routes the row (routing reads the column independently, so this can't corrupt anything) — but unusual enough to
	// flag, since an accidental bind to the type column is an easy mistake. Never changes the return.
	if (OutWarnings && !Mapping.DiscriminatorColumn.IsNone())
	{
		for (const FQuestColumnBinding& B : Mapping.Bindings)
		{
			if (B.SourceColumn == Mapping.DiscriminatorColumn)
			{
				OutWarnings->Add(FText::Format(
					LOCTEXT("DiscriminatorAlsoBound", "Column '{0}' is the discriminator and is also bound to property '{1}'. This is allowed (the value will both route the row and fill the property) — confirm you intend it."),
					FText::FromName(Mapping.DiscriminatorColumn), FText::FromName(B.TargetProperty)));
			}
		}
	}

	return OutErrors.Num() == ErrorsBefore;
}

TUniquePtr<ISimpleQuestDataFormat> MakeQuestDataFormat(const TArray<FString>& Args, const TCHAR* LogPrefix)
{
	FString ConsoleArgName;
	for (const FString& Arg : Args)
	{
		if (Arg.StartsWith(TEXT("--format=")))
		{
			ConsoleArgName = Arg.RightChop(9);   // length of "--format="
			break;
		}
	}
	const FString SettingsDefault = GetDefault<USimpleQuestSettings>()->DefaultImportFormat.ToString();

	FString Error;
	const FString Name = ResolveQuestDataFormatName(ConsoleArgName, /*MappingAsset*/ FString(), SettingsDefault, Error);
	if (Name.IsEmpty())
	{
		UE_LOG(LogSimpleQuest, Error, TEXT("%s: %s"), LogPrefix, *Error);
		return nullptr;
	}
	return FQuestDataFormatRegistry::Get().Create(Name);
}

#undef LOCTEXT_NAMESPACE
