// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

// Folder round-trip for the file maps ISimpleQuestDataFormat produces and consumes.
//
// This exists so that "serialize a bundle" and "put bytes on disk" are separate decisions. Formats do the first; this
// does the second; a caller that wants neither simply keeps the strings. Every in-tree caller that used to hand a
// folder to a provider now pairs one of these with the provider call, so folder behavior is unchanged - it just is
// not the only option any more.

#include "CoreMinimal.h"

namespace QuestDataFormatIO
{
	/**
	 * Writes each entry as DestFolder/<key>. Creates the folder tree if needed. Keys must be bare file names; a key
	 * containing a path separator is rejected rather than written, since a format emitting one would be choosing a
	 * layout outside the folder it was given.
	 */
	SIMPLEQUESTEDITOR_API bool WriteFilesToFolder(const TMap<FString, FString>& Files, const FString& DestFolder, FString& OutError);

	/**
	 * Gathers SrcFolder/ *.<Extension> into a file map keyed by bare file name. Non-recursive: a format's files sit
	 * together in one folder, and recursing would silently pick up unrelated data.
	 *
	 * An empty result is NOT an error - a folder with no matching files is a legitimate "nothing to import", and the
	 * caller decides whether that is a problem. OutError is set only for a folder that cannot be read at all.
	 */
	SIMPLEQUESTEDITOR_API bool ReadFilesFromFolder(const FString& SrcFolder, const FString& Extension, TMap<FString, FString>& OutFiles, FString& OutError);
}

