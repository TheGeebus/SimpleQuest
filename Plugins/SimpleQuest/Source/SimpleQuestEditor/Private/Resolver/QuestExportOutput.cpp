// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Resolver/QuestExportOutput.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"


const TCHAR* const GQuestExportMarkerName = TEXT(".simplequest-export");

bool ReadQuestExportMarker(const FString& Folder, FQuestExportMarker& Out)
{
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *(Folder / GQuestExportMarkerName))) return false;
	TArray<FString> Lines;
	Text.ParseIntoArrayLines(Lines, /*CullEmpty*/ false);
	for (const FString& Line : Lines)
	{
		FString Key, Value;
		if (!Line.Split(TEXT("="), &Key, &Value)) continue;   // comment/blank lines have no '='
		if (Key == TEXT("Format"))           { Out.Format = Value; }
		else if (Key == TEXT("SourceAsset")) { Out.SourceAsset = Value; }
		else if (Key == TEXT("Mapping"))     { Out.Mapping = Value; }   // absent in markers written before recipes were recorded
		else if (Key == TEXT("File"))        { Out.Files.Add(Value); }
	}
	return true;
}

bool WriteQuestExportMarker(const FString& Folder, const FQuestExportMarker& Marker)
{
	TArray<FString> Lines;
	Lines.Add(TEXT("# Written by SimpleQuest. It marks this folder as export output, which a later export of the same"));
	Lines.Add(TEXT("# questline will replace. Delete this file if you want your own edits here left alone — SimpleQuest"));
	Lines.Add(TEXT("# will then refuse to export here rather than overwrite them."));
	Lines.Add(FString::Printf(TEXT("Format=%s"), *Marker.Format));
	Lines.Add(FString::Printf(TEXT("SourceAsset=%s"), *Marker.SourceAsset));
	// Written even when empty, like its neighbours above: in a file a human opens, a present-but-blank line says "canonical,
	// no recipe" where an omitted one reads as an oversight.
	Lines.Add(FString::Printf(TEXT("Mapping=%s"), *Marker.Mapping));
	for (const FString& File : Marker.Files) { Lines.Add(FString::Printf(TEXT("File=%s"), *File)); }
	// Force UTF-8: SaveStringToFile's AutoDetect silently switches to UTF-16 the moment any non-ASCII character appears
	// in the text, which would make this adopter-facing file unreadable in a plain editor and inconsistent with the data
	// files beside it.
	return FFileHelper::SaveStringToFile(FString::Join(Lines, TEXT("\n")), *(Folder / GQuestExportMarkerName), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

/** Filenames directly in a folder (never recursive). Empty when the folder doesn't exist. */
TArray<FString> QuestExportFilesIn(const FString& Folder)
{
	TArray<FString> Found;
	IFileManager::Get().FindFiles(Found, *(Folder / TEXT("*")), true, false);
	return Found;
}

