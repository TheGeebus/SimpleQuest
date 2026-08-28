// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Resolver/QuestMappingSource.h"

#include "Resolver/QuestDataValue.h"
#include "SimpleQuestLog.h"
#include "Engine/DataTable.h"
#include "Resolver/ISimpleQuestDataFormat.h"
#include "Resolver/QuestDataBundle.h"
#include "Resolver/QuestDataFormatIO.h"
#include "Resolver/QuestDataFormatRegistry.h"
#include "Resolver/QuestDataValueBuilder.h"
#include "UObject/StructOnScope.h"
#include "Resolver/QuestImportMapping.h"
#include "Settings/SimpleQuestSettings.h"
#include "UObject/StructOnScope.h"
#include "Utilities/SimpleQuestEditorUtils.h"

#define LOCTEXT_NAMESPACE "SimpleQuestMappingSource"

const UQuestImportMapping* LoadQuestMappingArg(const TArray<FString>& Args)
{
	for (const FString& Arg : Args)
	{
		if (Arg.StartsWith(TEXT("--mapping=")))
		{
			return LoadObject<UQuestImportMapping>(nullptr, *Arg.RightChop(10));   // length of "--mapping="
		}
	}
	return nullptr;
}

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

	TMap<FString, FString> Files;
	FString GatherError;
	if (!QuestDataFormatIO::ReadFilesFromFolder(SourceFolder, Format->FileExtension(), Files, GatherError))
	{
		Result.Error = FText::Format(LOCTEXT("Ungatherable", "Couldn't read source at '{0}': {1}"),
			FText::FromString(SourceFolder), FText::FromString(GatherError));
		return Result;
	}

	FQuestDataBundle Bundle;
	if (!Format->ReadBundle(Files, Bundle))
	{
		Result.Error = FText::Format(LOCTEXT("Unreadable", "Couldn't read source at '{0}' as {1}."),
			FText::FromString(SourceFolder), FText::FromString(Format->FormatName()));
		return Result;
	}

	// The source is readable. Collect the UNION of value columns across every content table (all node rows share one
	// discriminator and one binding set, so a column that appears in ANY content table is bindable). Exclude the self-row
	// table - it isn't fanned-out source content. Detect a within-table duplicate: the parse already keyed cells by column
	// name (collapsing a repeated header), so a duplicate means the data is irrecoverably ambiguous - a blocking error.
	// The KEY column is gathered separately, and from EVERY table INCLUDING the self row: it is structural, one source
	// spells it the same way throughout, and it is never bindable - so it stays out of Columns and is reported beside them.
	TSet<FName> Seen;
	TSet<FName> KeyNames;
	for (const TPair<FString, FQuestDataTable>& TablePair : Bundle.TablesByType)
	{
		if (!TablePair.Value.KeyColumn.IsEmpty()) { KeyNames.Add(FName(*TablePair.Value.KeyColumn)); }
		if (TablePair.Key == TEXT("questline_graph")) continue;   // self row, not fanned source content

		TSet<FName> WithinTable;
		for (const FString& Col : TablePair.Value.Columns)
		{
			const FName ColName(*Col);
			if (WithinTable.Contains(ColName))
			{
				Result.bHasDuplicateColumns = true;
				Result.Error = FText::Format(LOCTEXT("DupColumn", "Source table '{0}' has a duplicate column '{1}' - its data is ambiguous."),
					FText::FromString(TablePair.Key), FText::FromName(ColName));
				return Result;   // refuse the whole source; a duplicate is a hard source-validity error
			}
			WithinTable.Add(ColName);
			Seen.Add(ColName);
		}
	}

	Result.Columns = Seen.Array();
	Result.Columns.Sort(FNameLexicalLess());   // stable, alphabetical for the dropdown

	// One agreed name is an answer. Several is not, and picking one would be a coin flip on TMap iteration order - so leave it
	// unset and say so. Unset shows up as nothing to choose, which is the truth, rather than an arbitrary name that looks decided.
	if (KeyNames.Num() == 1)
	{
		Result.KeyColumn = *KeyNames.CreateConstIterator();
	}
	else if (KeyNames.Num() > 1)
	{
		TArray<FName> Sorted = KeyNames.Array();
		Sorted.Sort(FNameLexicalLess());
		TArray<FString> AsText;
		for (const FName& Name : Sorted) { AsText.Add(Name.ToString()); }
		UE_LOG(LogSimpleQuestResolver, Warning, TEXT("EnumerateForeignFileColumns: '%s' does not agree on its key column across "
			"its tables (%s) - leaving it unset."), *SourceFolder, *FString::Join(AsText, TEXT(", ")));
	}

	Result.bReadable = true;
	return Result;
}

TArray<FString> EnumerateColumnDistinctValues(const FString& FormatName, const FString& SourceFolder, const FName& ColumnName)
{
	TArray<FString> Distinct;
	if (FormatName.IsEmpty() || SourceFolder.IsEmpty() || ColumnName.IsNone()) return Distinct;

	const TUniquePtr<ISimpleQuestDataFormat> Format = FQuestDataFormatRegistry::Get().Create(FormatName);
	if (!Format) return Distinct;

	TMap<FString, FString> Files;
	FString GatherError;
	if (!QuestDataFormatIO::ReadFilesFromFolder(SourceFolder, Format->FileExtension(), Files, GatherError)) return Distinct;

	FQuestDataBundle Bundle;
	if (!Format->ReadBundle(Files, Bundle)) return Distinct;

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
			if (Value.IsEmpty()) continue;               // an absent/empty discriminator cell names no kind - skip it
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
	// the same name), so no duplicate check is needed - this provenance is structurally unambiguous.
	// AUTHORED names, not internal ones. A Blueprint row struct stores a member as "key_21_26B6F1E3…" and shows the
	// designer "key" - and the ROW PLANNER already matches on the authored name, so returning the internal one here would
	// offer a picker full of columns that could never resolve against the very struct they came from.
	for (TFieldIterator<FProperty> It(RowStruct); It; ++It)
	{
		Result.Columns.Add(FName(*RowStruct->GetAuthoredNameForField(*It)));
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

	for (const FQuestDiscriminatorClass& Entry : Mapping.DiscriminatorClasses)
	{
		UClass* Cls = Entry.NodeClass.LoadSynchronous();   // resolve once per class (all its values share it)
		const bool bClassOK = (Cls != nullptr);

		for (const FString& RawValue : Entry.Values)
		{
			const FString Norm = NormalizeDiscriminatorValue(RawValue);

			if (const FString* Prior = NormToRaw.Find(Norm))
			{
				OutErrors.Add(FText::Format(
					LOCTEXT("NormKeyCollision", "Discriminator values '{0}' and '{1}' are the same after normalization - one would silently override the other."),
					FText::FromString(*Prior), FText::FromString(RawValue)));
				continue;
			}
			NormToRaw.Add(Norm, RawValue);

			if (!bClassOK)
			{
				OutErrors.Add(FText::Format(
					LOCTEXT("UnresolvableClass", "Discriminator value '{0}' maps to a class that can't be resolved."),
					FText::FromString(RawValue)));
				continue;
			}
			OutClassByNormValue.Add(Norm, Cls);
		}
	}
	return OutErrors.Num() == ErrorsBefore;
}

bool ValidateMappingAgainstSource(const UQuestImportMapping& Mapping, const TArray<FName>& ActualColumns,
								  const TArray<FString>& ActualDiscriminatorValues, TArray<FText>& OutErrors,
								  TArray<FText>* OutWarnings)
{
	const int32 ErrorsBefore = OutErrors.Num();

	// The class map: catches unresolvable-class + normalized-key-collision, and gives the accepted normalized-value set that
	// EXACTLY matches what QuestBundle_ApplyMapping will route (same builder, no drift). Its errors accumulate into OutErrors.
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
					LOCTEXT("NoneRequire", "Binding to '{0}' is unmapped (None) but its policy is Require - a value can't be required from a column that isn't mapped."),
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
				LOCTEXT("UnmappedValue", "Discriminator value '{0}' appears in the source but has no class mapping - its rows would be dropped."),
				FText::FromString(ActualValue)));
		}
	}

	// Advisory (non-fatal): the discriminator column is also bound to a property. Legal - the value populates the property AND
	// still routes the row (routing reads the column independently, so this can't corrupt anything) - but unusual enough to
	// flag, since an accidental bind to the type column is an easy mistake. Never changes the return.
	if (OutWarnings && !Mapping.DiscriminatorColumn.IsNone())
	{
		for (const FQuestColumnBinding& B : Mapping.Bindings)
		{
			if (B.SourceColumn == Mapping.DiscriminatorColumn)
			{
				OutWarnings->Add(FText::Format(
					LOCTEXT("DiscriminatorAlsoBound", "Column '{0}' is the discriminator and is also bound to property '{1}'. This is allowed (the value will both route the row and fill the property) - confirm you intend it."),
					FText::FromName(Mapping.DiscriminatorColumn), FText::FromName(B.TargetProperty)));
			}
		}
	}

	return OutErrors.Num() == ErrorsBefore;
}

FString ResolveQuestFormatNameFromArgs(const TArray<FString>& Args, const TCHAR* LogPrefix)
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
		UE_LOG(LogSimpleQuestResolver, Error, TEXT("%s: %s"), LogPrefix, *Error);
	}
	return Name;
}

TUniquePtr<ISimpleQuestDataFormat> MakeQuestDataFormat(const TArray<FString>& Args, const TCHAR* LogPrefix)
{
	const FString Name = ResolveQuestFormatNameFromArgs(Args, LogPrefix);
	return Name.IsEmpty() ? nullptr : FQuestDataFormatRegistry::Get().Create(Name);
}

namespace
{
	// A DataTable maps onto the bundle convention exactly: the ROW NAME is Row.Key, each row-struct property is a value
	// column. Unlike a text provider, this source has the FProperty in hand - so cells are built TYPED (Tag/Enum/Reference/
	// Array/StructLiteral) through the same builder the graph export uses, never degraded to strings.
	void BuildDataTableContent(const UDataTable& Table, const UScriptStruct& RowStruct, FQuestDataTable& OutContent)
	{
		for (TFieldIterator<FProperty> It(&RowStruct); It; ++It)
		{
			// AUTHORED name, not GetName(): a Blueprint-authored row struct mangles member names with a GUID suffix
			// ("type_5_A1B2..."), and a studio's DataTable is usually BP-authored.
			OutContent.Columns.Add(RowStruct.GetAuthoredNameForField(*It));
		}

		// A default-constructed row is the comparison basis, so a member sitting at its struct default emits Kind::Empty -
		// the same "absent == at-default" contract an omitted TSV cell carries. FStructOnScope handles alignment,
		// construction and teardown for us.
		FStructOnScope DefaultRow(&RowStruct);

		for (const TPair<FName, uint8*>& RowPair : Table.GetRowMap())
		{
			if (!RowPair.Value) continue;

			FQuestDataRow Row;
			Row.Key = RowPair.Key.ToString();
			for (TFieldIterator<FProperty> It(&RowStruct); It; ++It)
			{
				const void* ValuePtr   = It->ContainerPtrToValuePtr<void>(RowPair.Value);
				const void* DefaultPtr = It->ContainerPtrToValuePtr<void>(DefaultRow.GetStructMemory());

				// A DataTable row HAS a value for every field of its struct - absence is not expressible here at all. An
				// at-default field therefore yields an Empty cell that is still PRESENT: the source declared it and left it
				// at its default. Dropping it would make a typed source look narrower than it is, and would erase exactly the
				// distinction a re-import needs to tell "the author cleared this" from "the source never mentioned it".
				FQuestDataValue V = BuildQuestDataValue(*It, ValuePtr, DefaultPtr);
				Row.Cells.Add(RowStruct.GetAuthoredNameForField(*It), MoveTemp(V));
			}
			OutContent.Rows.Add(MoveTemp(Row));
		}
	}

	// A DataTable carries no self-row table, but the routing core requires exactly one. Synthesize it from the ASSET NAME -
	// the natural analogue of the folder name a file source is read from. Sanitized so it is a valid tag segment / package
	// name, matching what the export writes for a file round-trip.
	void SynthesizeDataTableSelfRow(const UDataTable& Table, FQuestDataBundle& OutBundle)
	{
		const FString AssetName = FSimpleQuestEditorUtilities::SanitizeQuestlineTagSegment(Table.GetName());

		FQuestDataTable Self;
		Self.Columns.Add(TEXT("class"));
		Self.Columns.Add(TEXT("QuestlineID"));

		FQuestDataRow SelfRow;
		SelfRow.Key = AssetName;
		SelfRow.Cells.Add(TEXT("class"), FQuestDataValue::MakeString(TEXT("QuestlineGraph")));
		SelfRow.Cells.Add(TEXT("QuestlineID"), FQuestDataValue::MakeString(AssetName));
		Self.Rows.Add(MoveTemp(SelfRow));

		OutBundle.TablesByType.Add(TEXT("questline_graph"), MoveTemp(Self));
		OutBundle.bSelfRowSynthesized = true;   // this identity is the framework's, not the author's - consumers must not diff it
	}
}

FQuestPlanEndpoint QuestPlanEndpointFrom(const FQuestDataEndpoint& Endpoint, const UQuestImportMapping* Mapping)
{
	FQuestPlanEndpoint OutEndpoint;
	OutEndpoint.Folder     = Endpoint.Folder;
	OutEndpoint.FormatName = Endpoint.FormatName;
	OutEndpoint.Table      = Endpoint.Table.ToSoftObjectPath();
	OutEndpoint.Mapping    = Mapping ? FSoftObjectPath(Mapping) : FSoftObjectPath();
	return OutEndpoint;
}

FQuestDataEndpoint QuestEndpointFromPlanSource(const FQuestPlanEndpoint& Source)
{
	FQuestDataEndpoint Endpoint;
	// The two provenances are exclusive, so the table decides. Deriving Kind here rather than storing it means the
	// pair can never disagree - which is the failure mode a hand-assembled endpoint kept reintroducing.
	Endpoint.Kind       = Source.Table.IsValid() ? EQuestEndpointKind::DataTable : EQuestEndpointKind::ForeignFile;
	Endpoint.Table      = TSoftObjectPtr<UDataTable>(Source.Table);
	Endpoint.Folder     = Source.Folder;
	Endpoint.FormatName = Source.FormatName;
	return Endpoint;
}

bool ReadEndpointBundle(const FQuestDataEndpoint& Endpoint, FQuestDataBundle& OutBundle, FString& OutError)
{
	if (Endpoint.Kind == EQuestEndpointKind::ForeignFile)
	{
		if (Endpoint.FormatName.IsEmpty() || Endpoint.Folder.IsEmpty())
		{
			OutError = TEXT("a file source needs both a format and a folder");
			return false;
		}
		const TUniquePtr<ISimpleQuestDataFormat> Format = FQuestDataFormatRegistry::Get().Create(Endpoint.FormatName);
		if (!Format)
		{
			OutError = FString::Printf(TEXT("format '%s' is not registered"), *Endpoint.FormatName);
			return false;
		}
		TMap<FString, FString> Files;
		FString GatherError;
		if (!QuestDataFormatIO::ReadFilesFromFolder(Endpoint.Folder, Format->FileExtension(), Files, GatherError))
		{
			OutError = FString::Printf(TEXT("could not read '%s': %s"), *Endpoint.Folder, *GatherError);
			return false;
		}
		if (!Format->ReadBundle(Files, OutBundle))
		{
			OutError = FString::Printf(TEXT("could not parse '%s' as %s (%d file(s))"),
				*Endpoint.Folder, *Format->FormatName(), Files.Num());
			return false;
		}
		return true;
	}

	// DataTable arm - no format, no file I/O; the row struct is the authority.
	const UDataTable* Table = Endpoint.Table.LoadSynchronous();
	if (!Table)
	{
		OutError = TEXT("the Data Table could not be loaded");
		return false;
	}
	const UScriptStruct* RowStruct = Table->GetRowStruct();
	if (!RowStruct)
	{
		OutError = FString::Printf(TEXT("Data Table '%s' has no row struct"), *Table->GetName());
		return false;
	}

	FQuestDataTable Content;
	BuildDataTableContent(*Table, *RowStruct, Content);
	if (Content.Rows.Num() == 0)
	{
		OutError = FString::Printf(TEXT("Data Table '%s' has no rows"), *Table->GetName());
		return false;
	}
	// One flat content table (the studio's rows are mixed kinds, routed later by the mapping's discriminator). The stem is
	// arbitrary so long as it isn't the reserved self-row key; "content" mirrors the flat-file convention.
	OutBundle.TablesByType.Add(TEXT("content"), MoveTemp(Content));
	SynthesizeDataTableSelfRow(*Table, OutBundle);
	return true;
}

#undef LOCTEXT_NAMESPACE
