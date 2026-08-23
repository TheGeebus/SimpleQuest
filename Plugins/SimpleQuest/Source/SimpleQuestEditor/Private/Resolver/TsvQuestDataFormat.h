// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

// PROTOTYPE — Resolver, Phase 3 Stage 2. The default format provider: tab-separated per-type tables + one edge table,
// the interlingua's on-disk form. Absorbs the TSV framing that used to live inline in the export/import prototypes
// (Sanitize/Unsanitize, the .tsv row/header layout, file discovery + parse). A plain class (not a UObject) — Stage 2 has
// exactly one provider, instantiated directly; a registry earns its place when Stage 3 adds a second format + selection.
// See notes-07-phase3-stage2-code-spec.txt §3.

#include "CoreMinimal.h"
#include "Resolver/ISimpleQuestDataFormat.h"

class FTsvQuestDataFormat final : public ISimpleQuestDataFormat
{
public:
	virtual bool WriteBundle(const FQuestDataBundle& Bundle, TMap<FString, FString>& OutFiles) override;
	virtual bool ReadBundle(const TMap<FString, FString>& Files, FQuestDataBundle& OutBundle) override;
	virtual FString FormatName() const override { return TEXT("TSV"); }
};