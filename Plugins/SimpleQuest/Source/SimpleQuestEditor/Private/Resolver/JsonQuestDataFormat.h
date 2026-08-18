// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

// PROTOTYPE — Resolver, Phase 3 Stage 3. The STRUCTURED format provider: one questline.json per questline, rendering
// each FQuestDataValue from its TYPED FIELDS (a tag as "SimpleQuest.Outcome.Reached", a bool as true, an array as [...]),
// NEVER from CanonicalText. This is what PROVES the neutral value is self-sufficient — a provider that round-trips using
// only the structured fields. Sibling to the TSV provider; the cross-provider B2 oracle confirms Graph->JSON->Graph is
// compiled-model-identical to Graph->TSV->Graph. See notes-07-phase3-stage3-json-design.txt.

#include "CoreMinimal.h"
#include "Resolver/ISimpleQuestDataFormat.h"

class FJsonQuestDataFormat final : public ISimpleQuestDataFormat
{
public:
	virtual bool WriteBundle(const FQuestDataBundle& Bundle, const FString& DestFolder) override;
	virtual bool ReadBundle(const FString& SrcFolder, FQuestDataBundle& OutBundle) override;
	virtual FString FormatName() const override { return TEXT("JSON"); }
};