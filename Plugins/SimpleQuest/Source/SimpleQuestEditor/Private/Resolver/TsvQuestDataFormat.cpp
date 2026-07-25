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

	// Parse one .tsv into a table (first line is the header; column 0 is always "key"). Cells become string-bearing
	// FQuestDataValues: CanonicalText = the unsanitized cell, Kind generic (the routing core types each against the
	// destination property — see spec §1). An empty/absent trailing field maps to no cell (== Kind::Empty downstream).
	bool ParseTsvTable(const FString& Path, FQuestDataTable& OutTable, TArray<FQuestDataRow>& OutRows)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Path)) return false;
		TArray<FString> Lines;
		Text.ParseIntoArrayLines(Lines, /*CullEmpty*/ false);
		if (Lines.Num() == 0) return false;

		// Header: "key\t<col1>\t<col2>...". Drop the leading "key" — Columns holds the value columns only (matching the
		// export layout, which prepends "key" at write time and stores only the value columns in Columns).
		TArray<FString> Header;
		Lines[0].ParseIntoArray(Header, TEXT("\t"), /*CullEmpty*/ false);
		for (int32 c = 1; c < Header.Num(); ++c)
		{
			OutTable.Columns.Add(Header[c]);
		}

		for (int32 i = 1; i < Lines.Num(); ++i)
		{
			if (Lines[i].IsEmpty()) continue;
			TArray<FString> Fields;
			Lines[i].ParseIntoArray(Fields, TEXT("\t"), /*CullEmpty*/ false);

			FQuestDataRow Row;
			Row.Key = Fields.IsValidIndex(0) ? Fields[0] : FString();
			for (int32 c = 0; c < OutTable.Columns.Num(); ++c)
			{
				const int32 FieldIdx = c + 1;   // +1 for the leading key column
				const FString Cell = Fields.IsValidIndex(FieldIdx) ? Unsanitize(Fields[FieldIdx]) : FString();
				if (Cell.IsEmpty())
				{
					continue;   // absent/empty cell == Kind::Empty (routing core leaves the property at its default)
				}
				FQuestDataValue V;
				V.Kind = EQuestDataValueKind::Scalar;   // generic string-bearing; the property types it downstream
				V.Scalar = Cell;
				V.CanonicalText = Cell;
				Row.Cells.Add(OutTable.Columns[c], V);
			}
			OutRows.Add(MoveTemp(Row));
		}
		return true;
	}

	bool ParseTsvEdges(const FString& Path, TArray<FQuestDataEdge>& Out)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Path)) return false;
		TArray<FString> Lines;
		Text.ParseIntoArrayLines(Lines, /*CullEmpty*/ false);
		for (int32 i = 1; i < Lines.Num(); ++i)   // skip "from\ttype\tto"
		{
			if (Lines[i].IsEmpty()) continue;
			TArray<FString> F;
			Lines[i].ParseIntoArray(F, TEXT("\t"), /*CullEmpty*/ false);
			if (F.Num() >= 3) Out.Add({ F[0], F[1], F[2] });
		}
		return true;
	}
}

bool FTsvQuestDataFormat::WriteBundle(const FQuestDataBundle& Bundle, const FString& DestFolder)
{
	IFileManager::Get().MakeDirectory(*DestFolder, /*Tree*/ true);

	TArray<FString> Stems;
	Bundle.TablesByType.GetKeys(Stems);
	Stems.Sort();
	for (const FString& Stem : Stems)
	{
		const FQuestDataTable& Table = Bundle.TablesByType[Stem];

		// Rows sorted by key for determinism. A local copy — WriteBundle takes a const bundle, so the sort that used to
		// happen in the export's WriteBundle moves here; the on-disk order is the provider's concern.
		TArray<FQuestDataRow> SortedRows = Table.Rows;
		SortedRows.Sort([](const FQuestDataRow& A, const FQuestDataRow& B) { return A.Key < B.Key; });

		TArray<FString> Lines;
		Lines.Add(FString::Printf(TEXT("key\t%s"), *FString::Join(Table.Columns, TEXT("\t"))));
		for (const FQuestDataRow& Row : SortedRows)
		{
			TArray<FString> Cells;
			Cells.Add(Row.Key);
			for (const FString& Col : Table.Columns)
			{
				const FQuestDataValue* Cell = Row.Cells.Find(Col);
				Cells.Add(Cell ? Sanitize(Cell->CanonicalText) : FString());
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
			UE_LOG(LogSimpleQuest, Warning, TEXT("TsvQuestDataFormat: failed to write '%s'."), *Path);
			return false;
		}
		UE_LOG(LogSimpleQuest, Verbose, TEXT("TsvQuestDataFormat: wrote '%s' (%d row(s))."), *Path, SortedRows.Num());
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
	const FString EdgePath = DestFolder / TEXT("edges.tsv");
	if (!FFileHelper::SaveStringToFile(FString::Join(EdgeLines, TEXT("\n")), *EdgePath))
	{
		UE_LOG(LogSimpleQuest, Warning, TEXT("TsvQuestDataFormat: failed to write '%s'."), *EdgePath);
		return false;
	}
	UE_LOG(LogSimpleQuest, Verbose, TEXT("TsvQuestDataFormat: wrote '%s' (%d edge(s))."), *EdgePath, SortedEdges.Num());
	return true;
}

bool FTsvQuestDataFormat::ReadBundle(const FString& SrcFolder, FQuestDataBundle& OutBundle)
{
	if (!FPaths::DirectoryExists(SrcFolder))
	{
		UE_LOG(LogSimpleQuest, Warning, TEXT("TsvQuestDataFormat: folder not found '%s'."), *SrcFolder);
		return false;
	}

	TArray<FString> TsvFiles;
	IFileManager::Get().FindFiles(TsvFiles, *(SrcFolder / TEXT("*.tsv")), /*Files*/ true, /*Dirs*/ false);
	if (TsvFiles.Num() == 0)
	{
		UE_LOG(LogSimpleQuest, Warning, TEXT("TsvQuestDataFormat: no .tsv files in '%s'."), *SrcFolder);
		return false;
	}

	for (const FString& File : TsvFiles)
	{
		if (File == TEXT("edges.tsv"))
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