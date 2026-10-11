// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Utilities/QuestOrdering.h"

#include "Algo/Reverse.h"
#include "Misc/ComparisonUtility.h"
#include "Quests/Types/QuestEntryRecord.h"
#include "Subsystems/QuestStateSubsystem.h"

namespace
{
	/** The tag and every ancestor above it, root first - the path that the comparison walks down. */
	void BuildAncestorChain(FGameplayTag Tag, TArray<FGameplayTag>& OutChain)
	{
		OutChain.Reset();
		for (FGameplayTag Current = Tag; Current.IsValid(); Current = Current.RequestDirectParent())
		{
			OutChain.Add(Current);
		}
		Algo::Reverse(OutChain);
	}

	// An absent key has to sort LAST, and its sentinel is negative, so each is mapped to the top of its range before
	// anything is compared. Normalizing here rather than branching at the comparison is what keeps every step below a
	// plain "is this less than that" - no branch that could disagree with another branch.
	int32 IndexRank(const int32 Value) { return Value == INDEX_NONE ? MAX_int32 : Value; }
	double TimeRank(const double Value) { return Value < 0.0 ? TNumericLimits<double>::Max() : Value; }
}

bool FQuestOrdering::Less(const FGameplayTag A, const FGameplayTag B, const FOrderingKeyLookup GetKeys)
{
	if (A == B || !A.IsValid() || !B.IsValid())
	{
		return false;
	}

	TArray<FGameplayTag> ChainA;
	TArray<FGameplayTag> ChainB;
	BuildAncestorChain(A, ChainA);
	BuildAncestorChain(B, ChainB);

	// Walk down the shared prefix to the first level where the two differ.
	int32 Depth = 0;
	const int32 Shallowest = FMath::Min(ChainA.Num(), ChainB.Num());
	while (Depth < Shallowest && ChainA[Depth] == ChainB[Depth])
	{
		++Depth;
	}

	// One is an ancestor of the other: its chain ran out while still matching. Structure decides and there is nothing
	// to compare - a parent is always presented before its children, whatever anyone authored.
	if (Depth == Shallowest)
	{
		return ChainA.Num() < ChainB.Num();
	}

	// Keys are read at the DIVERGENCE, never at the leaves. Two steps in different chapters are ordered by their
	// chapters, so nothing authored on a step can reorder the chapters above it. Same shape as comparing two file
	// paths: the first differing segment decides and nothing past it is consulted.
	const FGameplayTag DivergeA = ChainA[Depth];
	const FGameplayTag DivergeB = ChainB[Depth];
	const FQuestOrderingKeys KeysA = GetKeys(DivergeA);
	const FQuestOrderingKeys KeysB = GetKeys(DivergeB);

	// The authored walk: one integer already carrying precedence AND bias. See the header for why that is a single key
	// here rather than two rules consulted in turn.
	const int32 PositionA = IndexRank(KeysA.AuthoredPosition);
	const int32 PositionB = IndexRank(KeysB.AuthoredPosition);
	if (PositionA != PositionB)
	{
		return PositionA < PositionB;
	}

	// Nothing authored relates them - two assets that never refer to each other. When they started is all there is.
	const double TimeA = TimeRank(KeysA.EntryTime);
	const double TimeB = TimeRank(KeysB.EntryTime);
	if (TimeA != TimeB)
	{
		return TimeA < TimeB;
	}

	// Same frame. The clock cannot separate them, which is the whole reason a sequence is stamped beside it.
	const int32 SequenceA = IndexRank(KeysA.EntrySequence);
	const int32 SequenceB = IndexRank(KeysB.EntrySequence);
	if (SequenceA != SequenceB)
	{
		return SequenceA < SequenceB;
	}

	return UE::ComparisonUtility::CompareNaturalOrder(DivergeA.ToString(), DivergeB.ToString()) < 0;
}

bool FQuestOrdering::AuthoredSiblingLess(const FAuthoredSiblingKeys& A, const FAuthoredSiblingKeys& B, const FName NameA, const FName NameB)
{
	if (A.OrderBias != B.OrderBias) return A.OrderBias > B.OrderBias;       // higher sorts earlier
	if (A.LayoutY != B.LayoutY)     return A.LayoutY < B.LayoutY;           // top to bottom
	if (A.LayoutX != B.LayoutX)     return A.LayoutX < B.LayoutX;           // then left to right
	return NameA.LexicalLess(NameB);
}

void FQuestOrdering::SortTags(TArray<FGameplayTag>& Tags, const FOrderingKeyLookup GetKeys)
{
	Tags.Sort([&GetKeys](const FGameplayTag& A, const FGameplayTag& B)
	{
		return FQuestOrdering::Less(A, B, GetKeys);
	});
}

namespace
{
	/** Builds the key pair from the runtime registry. Null subsystem reports neutral keys, so ordering degrades to
	 *  natural order rather than to something arbitrary. */
	auto MakeSubsystemLookup(const UQuestStateSubsystem* QuestState)
	{
		return [QuestState](const FGameplayTag Tag)
		{
			FQuestOrdering::FQuestOrderingKeys Keys;
			if (QuestState)
			{
				Keys.AuthoredPosition = QuestState->GetAuthoredPosition(Tag);
				const FQuestEntryRecord* Entry = QuestState->GetQuestEntry(Tag);
				if (Entry && !Entry->History.IsEmpty())
				{
					Keys.EntryTime = Entry->History[0].EntryTime;
					Keys.EntrySequence = Entry->History[0].EntrySequence;
				}
			}
			return Keys;
		};
	}
}

bool FQuestOrdering::Less(const FGameplayTag A, const FGameplayTag B, const UQuestStateSubsystem* QuestState)
{
	auto Lookup = MakeSubsystemLookup(QuestState);
	return Less(A, B, FOrderingKeyLookup(Lookup));
}

void FQuestOrdering::SortTags(TArray<FGameplayTag>& Tags, const UQuestStateSubsystem* QuestState)
{
	auto Lookup = MakeSubsystemLookup(QuestState);
	SortTags(Tags, FOrderingKeyLookup(Lookup));
}

