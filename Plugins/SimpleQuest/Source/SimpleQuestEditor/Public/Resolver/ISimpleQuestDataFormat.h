// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

// The bidirectional pluggable quest-data format contract. The routing core produces/consumes a format-free
// FQuestDataBundle (the interlingua); a provider translates that bundle to and from its own on-disk (or
// in-memory) form. Import/export SYMMETRY is the provider's own contract — the round-trip oracle tests it.
// A studio implements this interface to fold SimpleQuest into their own data pipeline without touching,
// forking, or understanding the graph walk. TSV is the default provider (Private/Resolver/TsvQuestDataFormat).
//
// READ-SIDE CONTRACT: a provider reading a text format CANNOT know a cell's domain Kind from the text
// alone (only the destination FProperty does), so ReadBundle produces STRING-BEARING values (CanonicalText populated,
// Kind generic) and the routing core types each against the live property. Structural validation of the bundle is the
// routing core's job, not the provider's.

#include "CoreMinimal.h"

struct FQuestDataBundle;

class ISimpleQuestDataFormat
{
public:
	virtual ~ISimpleQuestDataFormat() = default;

	// Neutral bundle -> the provider's form under DestFolder. The provider owns framing, escaping, file layout, and its
	// own value-rendering (tag / text / enum -> its representation). Returns false on any write failure.
	// NOT pure: implement only the directions your format supports. The default reports "unsupported" by returning false,
	// so a read-only provider needs no write stub.
	virtual bool WriteBundle(const FQuestDataBundle& Bundle, const FString& DestFolder) { return false; }

	// The provider's form under SrcFolder -> neutral bundle. The provider owns parsing. A provider reading a TEXT format
	// produces string-bearing cells (the routing core types them against the live property); one reading TYPED data can
	// emit properly-Kinded cells directly — see Resolver/QuestDataValueBuilder.h. Structural validity is checked
	// downstream by the routing core. NOT pure, for the same reason as WriteBundle.
	virtual bool ReadBundle(const FString& SrcFolder, FQuestDataBundle& OutBundle) { return false; }

	// A short identifier for logging and the provider registry. Every provider must identify itself, so this stays required.
	virtual FString FormatName() const = 0;
};