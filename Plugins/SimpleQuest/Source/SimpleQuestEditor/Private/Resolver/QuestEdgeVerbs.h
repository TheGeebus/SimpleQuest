// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

// The wiring vocabulary: the verbs a data source writes, and the pin categories they name. ONE list, because there were
// three - a category->verb map in the export path, a verb->category map in the import path, and a bare literal array
// feeding the recipe's picker, with only the first two even referencing each other. Adding a fifth verb meant editing
// three files in two modules with nothing to catch a miss, and a miss would surface as a wire that exports and refuses
// to come back.
// Public because the picker that needs it is a UFUNCTION on a Public header; everything else here follows it.

#include "CoreMinimal.h"

namespace QuestEdgeVerbs
{
	struct FEntry
	{
		const TCHAR* Verb;
		const TCHAR* PinCategory;
	};

	/** THE list. Everything below derives from it, and nothing else should spell these strings. */
	inline const TArray<FEntry>& All()
	{
		static const TArray<FEntry> Entries =
		{
			{ TEXT("activates"),    TEXT("QuestActivation")		},
			{ TEXT("outcome"),      TEXT("QuestOutcome")			},
			{ TEXT("feeds-prereq"), TEXT("QuestPrerequisite")	},
			// Output-side category is past-tense "QuestDeactivated" - the input side's "QuestDeactivate" never appears as
			// an edge source, because sources are always output pins.
			{ TEXT("deactivates"),  TEXT("QuestDeactivated")		},
		};
		return Entries;
	}

	/** Empty when the category names no known verb; the caller decides how to spell that. */
	inline FString VerbForPinCategory(FName PinCategory)
	{
		for (const FEntry& E : All())
		{
			if (PinCategory == FName(E.PinCategory)) { return FString(E.Verb); }
		}
		return FString();
	}

	inline FName PinCategoryForVerb(const FString& Verb)
	{
		for (const FEntry& E : All())
		{
			if (Verb == E.Verb) { return FName(E.PinCategory); }
		}
		return NAME_None;
	}

	inline TArray<FString> Options()
	{
		TArray<FString> Out;
		Out.Reserve(All().Num());
		for (const FEntry& E : All()) { Out.Add(E.Verb); }
		return Out;
	}
}

