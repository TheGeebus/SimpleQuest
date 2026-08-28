// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

// The bidirectional pluggable quest-data format contract. The routing core produces/consumes a format-free
// FQuestDataBundle (the interlingua); a provider translates that bundle to and from its own on-disk (or
// in-memory) form. Import/export SYMMETRY is the provider's own contract - the round-trip oracle tests it.
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

	// Neutral bundle -> the provider's form, as file name -> file content. The provider owns framing, escaping, the
	// file layout it emits, and its own value-rendering (tag / text / enum -> its representation). Returns false on any
	// failure. Keys are bare file names WITH extension ("Nodes.tsv", "bundle.json"), never paths.
	//
	// TEXT, NOT DISK. A format serializes; it does not choose where bytes live. That separation is what lets an
	// external caller export a questline and receive strings, rather than having to invent a folder, let us write into
	// it, and read it back. Folder round-trips are still available and unchanged in behavior - see
	// Resolver/QuestDataFormatIO.h, which every in-tree caller now goes through.
	//
	// NOT pure: implement only the directions your format supports. The default reports "unsupported" by returning
	// false, so a read-only provider needs no write stub.
	virtual bool WriteBundle(const FQuestDataBundle& Bundle, TMap<FString, FString>& OutFiles) { return false; }

	// The provider's form -> neutral bundle, taking file name -> file content. The provider owns parsing. A provider
	// reading a TEXT format produces string-bearing cells (the routing core types them against the live property); one
	// reading TYPED data can emit properly-Kinded cells directly - see Resolver/QuestDataValueBuilder.h. Structural
	// validity is checked downstream by the routing core. NOT pure, for the same reason as WriteBundle.
	//
	// DISCOVERY IS THE CALLER'S JOB NOW. Previously a provider scanned the folder to find its own files; now it is
	// handed exactly the set to parse. A caller with data in hand already knows what it has, and a caller starting
	// from a folder uses the gather helper, which selects by FileExtension().
	virtual bool ReadBundle(const TMap<FString, FString>& Files, FQuestDataBundle& OutBundle) { return false; }

	// A short identifier for logging and the provider registry. Every provider must identify itself, so this stays required.
	virtual FString FormatName() const = 0;

	// Extension this format's files carry, without the dot. Used when gathering a folder into a file map. Defaults to
	// the lowercased format name, which is correct for both shipped providers ("TSV" -> tsv, "JSON" -> json); override
	// if your format's name and extension differ.
	virtual FString FileExtension() const { return FormatName().ToLower(); }
};

