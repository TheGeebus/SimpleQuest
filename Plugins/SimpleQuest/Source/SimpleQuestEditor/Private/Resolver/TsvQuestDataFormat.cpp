// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Resolver/TsvQuestDataFormat.h"

#include "Resolver/QuestDataBundle.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "SimpleQuestLog.h"

namespace
{
	// Escape tabs/newlines in a serialized value so a value with embedded whitespace can't break the TSV row. (Moved
	// verbatim from the export prototype — TSV framing is the provider's concern.)
	FString Sanitize(const FString& In)
	{
		return In.Replace(TEXT("\t"), TEXT("\\t")).Replace(TEXT("\r"), TEXT("")).Replace(TEXT("\n"), TEXT("\\n"));
	}

	// Reverse of Sanitize — restore escaped whitespace in a parsed cell. (Moved verbatim from the import prototype.)
	FString Unsanitize(const FString& In)
	{
		return In.Replace(TEXT("\\n"), TEXT("\n")).Replace(TEXT("\\t"), TEXT("\t"));
	}

	// Render a structured FQuestDataValue to its TSV cell string — from the TYPED FIELDS the inverse of ParseTsvTable).
	// Kinds whose value already IS the string (Scalar/Enum/Reference/StructLiteral) return V.StringForm directly; the
	// structured Kinds (Tag/TagContainer/Text/Bool/Array) re-serialize via ExportText / FTextStringHelper::WriteToBuffer —
	// the same engine calls the reflection walk uses, so the on-disk form matches the on-disk form is byte-identical to
	// pre-Stage-4 TSV output.
	FString RenderValueToTsv(const FQuestDataValue& V)
	{
		switch (V.Kind)
		{
		case EQuestDataValueKind::Empty:
			return FString();

		case EQuestDataValueKind::Bool:
			return V.bBool ? TEXT("True") : TEXT("False");

		case EQuestDataValueKind::Text:
		{
			// Empty FText -> empty cell (import's empty-cell skip then leaves the default, keeping the round-trip symmetric).
			if (V.Text.IsEmpty()) return FString();
			FString Out;
			FTextStringHelper::WriteToBuffer(Out, V.Text, /*bRequiresQuotes*/ true, /*bStripPackageNamespace*/ false);
			return Out;
		}

		case EQuestDataValueKind::Tag:
		{
			FString Out;
			FGameplayTag Tmp = V.Tag;   // ExportText takes a mutable value ptr
			TBaseStructure<FGameplayTag>::Get()->ExportText(Out, &Tmp, /*Default*/ nullptr, /*Owner*/ nullptr, PPF_None, /*RootScope*/ nullptr);
			return Out;
		}

		case EQuestDataValueKind::TagContainer:
		{
			FString Out;
			FGameplayTagContainer Tmp = V.TagContainer;
			TBaseStructure<FGameplayTagContainer>::Get()->ExportText(Out, &Tmp, nullptr, nullptr, PPF_None, nullptr);
			return Out;
		}

		case EQuestDataValueKind::Array:
		{
			// ExportTextItem's array form: "(elem,elem,...)" — recurse per element, comma-join, wrap in parens.
			TArray<FString> Parts;
			Parts.Reserve(V.Elements.Num());
			for (const FQuestDataValue& Elem : V.Elements)
			{
				Parts.Add(RenderValueToTsv(Elem));
			}
			return FString::Printf(TEXT("(%s)"), *FString::Join(Parts, TEXT(",")));
		}

		case EQuestDataValueKind::Number:
		case EQuestDataValueKind::String:
		case EQuestDataValueKind::Enum:
		case EQuestDataValueKind::Reference:
		case EQuestDataValueKind::StructLiteral:
		default:
			// The value already IS the string form (numeric text / plain string / enum token / soft path / opaque
			// literal). TSV is all-text, so Number and String render identically here — the split matters only to a
			// structured provider (like JSON). The property types it on import.
			return V.StringForm;
		}
	}

	// Parse one .tsv into a table (first line is the header; column 0 is always "key"). Cells become string-bearing
	// FQuestDataValues: Scalar = the unsanitized cell, Kind generic (the routing core types each against the
	// destination property). An empty/absent trailing field maps to no cell (== Kind::Empty downstream).
	bool ParseTsvTable(const FString& Path, FQuestDataTable& OutTable, TArray<FQuestDataRow>& OutRows)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Path)) return false;
		TArray<FString> Lines;
		Text.ParseIntoArrayLines(Lines, false);
		if (Lines.Num() == 0) return false;

		// Header: "<key>\t<col1>\t<col2>...". Column 0 is the key whatever it is called, and Columns holds the value columns
		// only - but the key's header is RECORDED rather than dropped. It is the studio's own word for the column, it cannot be
		// reconstructed from anything else in the table, and every consumer that has to name the key needs it.
		TArray<FString> Header;
		Lines[0].ParseIntoArray(Header, TEXT("\t"), false);
		if (Header.Num() > 0)
		{
			OutTable.KeyColumn = Header[0];
		}
		for (int32 c = 1; c < Header.Num(); ++c)
		{
			OutTable.Columns.Add(Header[c]);
		}

		for (int32 i = 1; i < Lines.Num(); ++i)
		{
			if (Lines[i].IsEmpty()) continue;
			TArray<FString> Fields;
			Lines[i].ParseIntoArray(Fields, TEXT("\t"), false);

			FQuestDataRow Row;
			Row.Key = Fields.IsValidIndex(0) ? Fields[0] : FString();
			for (int32 c = 0; c < OutTable.Columns.Num(); ++c)
			{
				const int32 FieldIdx = c + 1;   // +1 for the leading key column
				const FString Cell = Fields.IsValidIndex(FieldIdx) ? Unsanitize(Fields[FieldIdx]) : FString();

				// A column the HEADER declares still gets a cell on a row that leaves it blank — an Empty one. That is a
				// positive statement ("this field is at its default"), and it is not the same as a column the source never
				// declared, which still produces no cell at all. Consumers that only want the value are unaffected: Get()
				// reads "" either way, and the restore path's Empty arm leaves the destination property untouched.
				FQuestDataValue V;
				if (Cell.IsEmpty())
				{
					V.Kind = EQuestDataValueKind::Empty;
				}
				else
				{
					V.Kind = EQuestDataValueKind::String;   // TSV is all-text: produce generic String; the property types it downstream
					V.StringForm = Cell;
				}
				Row.Cells.Add(OutTable.Columns[c], V);
			}
			OutRows.Add(MoveTemp(Row));
		}
		return true;
	}

	// The canonical edge-table filename we WRITE. Reading no longer keys on this name — a table is recognized as the edge
	// table by its column SIGNATURE (from/type/to), so a studio's differently-named relation file (e.g. connections.tsv)
	// still parses. This name is only the default output.
	const TCHAR* GEdgeTableDefaultName = TEXT("edges.tsv");

	// Is this file's header the edge signature? An edge table's first line is exactly "from\ttype\tto", and nothing else has
	// three columns spelled that way - a content table's first column is its KEY, under whatever name the studio gave it, and
	// the remaining two would have to be "type" and "to" for the shapes to collide. The header alone identifies it.
	bool FileHasEdgeSignature(const FString& Path)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Path)) return false;
		TArray<FString> Lines;
		Text.ParseIntoArrayLines(Lines, false);
		if (Lines.Num() == 0) return false;
		TArray<FString> Header;
		Lines[0].ParseIntoArray(Header, TEXT("\t"), false);
		return Header.Num() == 3 && Header[0] == TEXT("from") && Header[1] == TEXT("type") && Header[2] == TEXT("to");
	}

	bool ParseTsvEdges(const FString& Path, TArray<FQuestDataEdge>& Out)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Path)) return false;
		TArray<FString> Lines;
		Text.ParseIntoArrayLines(Lines, false);
		for (int32 i = 1; i < Lines.Num(); ++i)   // skip "from\ttype\tto"
		{
			if (Lines[i].IsEmpty()) continue;
			TArray<FString> F;
			Lines[i].ParseIntoArray(F, TEXT("\t"), false);
			if (F.Num() >= 3) Out.Add({ F[0], F[1], F[2] });
		}
		return true;
	}
}

bool FTsvQuestDataFormat::WriteBundle(const FQuestDataBundle& Bundle, const FString& DestFolder)
{
	IFileManager::Get().MakeDirectory(*DestFolder, true);

	TArray<FString> Stems;
	Bundle.TablesByType.GetKeys(Stems);
	Stems.Sort();
	for (const FString& Stem : Stems)
	{
		const FQuestDataTable& Table = Bundle.TablesByType[Stem];

		// Rows sorted by key for determinism. A local copy. WriteBundle takes a const bundle, so the sort that used to
		// happen in the export's WriteBundle moves here; the on-disk order is the provider's concern.
		TArray<FQuestDataRow> SortedRows = Table.Rows;
		SortedRows.Sort([](const FQuestDataRow& A, const FQuestDataRow& B) { return A.Key < B.Key; });

		TArray<FString> Lines;
		// The key goes out under the name the TABLE carries, so a bundle that knows what its key was CALLED writes that back
		// instead of flattening it to ours. Empty means the table was built rather than read, and that is the arm every export
		// actually takes today - the graph builds its own tables, so nothing in the current pipeline carries a read name here.
		// Kept anyway, and covered by a test: a writer that ignores a field its own type carries is a trap for whoever adds
		// the first read-to-write path, and it would fail silently by renaming somebody's column.
		const FString KeyHeader = Table.KeyColumn.IsEmpty() ? FString(TEXT("key")) : Table.KeyColumn;
		Lines.Add(FString::Printf(TEXT("%s\t%s"), *KeyHeader, *FString::Join(Table.Columns, TEXT("\t"))));
		for (const FQuestDataRow& Row : SortedRows)
		{
			TArray<FString> Cells;
			Cells.Add(Row.Key);
			for (const FString& Col : Table.Columns)
			{
				const FQuestDataValue* Cell = Row.Cells.Find(Col);
				Cells.Add(Cell ? Sanitize(RenderValueToTsv(*Cell)) : FString());
			}
#if !UE_BUILD_SHIPPING
			// Drift guard: a cell not in Columns means per-class column capture diverged from a row's actual cells.
			for (const TPair<FString, FQuestDataValue>& CellPair : Row.Cells)
			{
				ensureMsgf(Table.Columns.Contains(CellPair.Key),
					TEXT("TsvQuestDataFormat: row '%s' carries cell '%s' absent from '%s' columns."), *Row.Key, *CellPair.Key, *Stem);
			}
#endif
			Lines.Add(FString::Join(Cells, TEXT("\t")));
		}

		const FString Path = DestFolder / (Stem + TEXT(".tsv"));
		if (!FFileHelper::SaveStringToFile(FString::Join(Lines, TEXT("\n")), *Path))
		{
			UE_LOG(LogSimpleQuestResolver, Warning, TEXT("TsvQuestDataFormat: failed to write '%s'."), *Path);
			return false;
		}
		UE_LOG(LogSimpleQuestResolver, Verbose, TEXT("TsvQuestDataFormat: wrote '%s' (%d row(s))."), *Path, SortedRows.Num());
	}

	TArray<FQuestDataEdge> SortedEdges = Bundle.Edges;
	SortedEdges.Sort([](const FQuestDataEdge& A, const FQuestDataEdge& B)
	{
		if (A.From != B.From) return A.From < B.From;
		if (A.Type != B.Type) return A.Type < B.Type;
		return A.To < B.To;
	});
	TArray<FString> EdgeLines;
	EdgeLines.Add(TEXT("from\ttype\tto"));
	for (const FQuestDataEdge& E : SortedEdges)
	{
		EdgeLines.Add(FString::Printf(TEXT("%s\t%s\t%s"), *E.From, *E.Type, *E.To));
	}
	const FString EdgePath = DestFolder / GEdgeTableDefaultName;
	if (!FFileHelper::SaveStringToFile(FString::Join(EdgeLines, TEXT("\n")), *EdgePath))
	{
		UE_LOG(LogSimpleQuestResolver, Warning, TEXT("TsvQuestDataFormat: failed to write '%s'."), *EdgePath);
		return false;
	}
	UE_LOG(LogSimpleQuestResolver, Verbose, TEXT("TsvQuestDataFormat: wrote '%s' (%d edge(s))."), *EdgePath, SortedEdges.Num());
	return true;
}

bool FTsvQuestDataFormat::ReadBundle(const FString& SrcFolder, FQuestDataBundle& OutBundle)
{
	if (!FPaths::DirectoryExists(SrcFolder))
	{
		UE_LOG(LogSimpleQuestResolver, Warning, TEXT("TsvQuestDataFormat: folder not found '%s'."), *SrcFolder);
		return false;
	}

	TArray<FString> TsvFiles;
	IFileManager::Get().FindFiles(TsvFiles, *(SrcFolder / TEXT("*.tsv")), /*Files*/ true, /*Dirs*/ false);
	if (TsvFiles.Num() == 0)
	{
		UE_LOG(LogSimpleQuestResolver, Warning, TEXT("TsvQuestDataFormat: no .tsv files in '%s'."), *SrcFolder);
		return false;
	}

	for (const FString& File : TsvFiles)
	{
		// The edge table is recognized by its column signature (from/type/to), NOT its filename - so a studio's own-named
		// relation file parses as edges, and our default edges.tsv still round-trips (it carries the same signature).
		if (FileHasEdgeSignature(SrcFolder / File))
		{
			if (!ParseTsvEdges(SrcFolder / File, OutBundle.Edges)) return false;
			continue;
		}
		const FString Stem = FPaths::GetBaseFilename(File);
		FQuestDataTable Table;
		TArray<FQuestDataRow> Rows;
		if (!ParseTsvTable(SrcFolder / File, Table, Rows)) return false;
		Table.Rows = MoveTemp(Rows);
		OutBundle.TablesByType.Add(Stem, MoveTemp(Table));
	}
	return true;
}