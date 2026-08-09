// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

// The comparison half of the round-trip check. Two questions, asked separately: did the AUTHORED content survive an
// export/import cycle, and did the resulting BEHAVIOUR survive it? Each comparator normalizes away the differences that
// are known-benign - the reconstructed asset's identity suffix, per-package loc namespaces, object paths, container
// order - so a reported difference is one that actually means something. Owns no model code.

#include "CoreMinimal.h"

/**
 * Compare a source export folder against the re-export of its reconstructed copy, file by file. Returns the number of
 * mismatches; zero means the authored content round-tripped intact. OriginalID names the questline so the copy's
 * identity suffix can be normalized out before diffing.
 */
int32 CompareQuestExportFolders(const FString& SrcFolder, const FString& RtFolder, const FString& OriginalID);

/**
 * Compare two compiled-model dumps, order-preserving. Returns the number of differing lines; zero means both assets
 * compile to the same behaviour. A missing dump on either side counts as one difference rather than a silent pass.
 */
int32 CompareQuestCompiledDumps(const FString& SrcDump, const FString& RtDump, const FString& OriginalID);

