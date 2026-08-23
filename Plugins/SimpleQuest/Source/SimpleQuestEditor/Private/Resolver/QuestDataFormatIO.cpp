// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Resolver/QuestDataFormatIO.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace QuestDataFormatIO
{
	bool WriteFilesToFolder(const TMap<FString, FString>& Files, const FString& DestFolder, FString& OutError)
	{
		OutError.Reset();
		if (DestFolder.IsEmpty())
		{
			OutError = TEXT("No destination folder given.");
			return false;
		}

		IFileManager::Get().MakeDirectory(*DestFolder, true);

		for (const TPair<FString, FString>& File : Files)
		{
			// A key with a separator would mean the format chose a layout outside the folder it was handed. Refuse it
			// rather than create directories a caller did not ask for.
			if (File.Key.Contains(TEXT("/")) || File.Key.Contains(TEXT("\\")))
			{
				OutError = FString::Printf(TEXT("File name '%s' contains a path separator; format keys must be bare file names."), *File.Key);
				return false;
			}

			const FString Path = DestFolder / File.Key;
			if (!FFileHelper::SaveStringToFile(File.Value, *Path))
			{
				OutError = FString::Printf(TEXT("Could not write '%s'."), *Path);
				return false;
			}
		}
		return true;
	}

	bool ReadFilesFromFolder(const FString& SrcFolder, const FString& Extension,
		TMap<FString, FString>& OutFiles, FString& OutError)
	{
		OutError.Reset();
		OutFiles.Reset();

		if (SrcFolder.IsEmpty() || !IFileManager::Get().DirectoryExists(*SrcFolder))
		{
			OutError = FString::Printf(TEXT("Source folder '%s' does not exist."), *SrcFolder);
			return false;
		}

		TArray<FString> Found;
		IFileManager::Get().FindFiles(Found, *(SrcFolder / FString::Printf(TEXT("*.%s"), *Extension)), true, false);

		for (const FString& Name : Found)
		{
			FString Text;
			if (!FFileHelper::LoadFileToString(Text, *(SrcFolder / Name)))
			{
				OutError = FString::Printf(TEXT("Could not read '%s'."), *(SrcFolder / Name));
				return false;
			}
			OutFiles.Add(Name, MoveTemp(Text));
		}
		return true;
	}
}

