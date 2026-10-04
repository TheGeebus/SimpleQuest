// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "NativeGameplayTags.h"
#include "Utilities/QuestOrdering.h"

/**
 * Fixture tags, declared natively in this translation unit rather than drawn from shipped content - the same reasoning as
 * QuestAdvancementHoldTests: native tags register at static init, reach no ini, and depend on no asset, so slimming a
 * QuickStart chapter cannot break them.
 *
 * The shapes are deliberate. OrderFixture.* are tag SIBLINGS with a containment relation between them - Chapter 4's
 * shape, and the case a tag hierarchy alone cannot describe. OrderBranch*.Leaf are two leaves under two parents, for
 * proving bias is read where the tags diverge rather than at the leaves. OrderRoot* stand in for two questlines each
 * started on its own, and OrderChapter_9 / _10 are the natural-order case plain alphabetical gets wrong.
 */
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Order_Fixture,     "SimpleQuest.Questline.OrderFixture");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Order_First,       "SimpleQuest.Questline.OrderFixture.First");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Order_Second,      "SimpleQuest.Questline.OrderFixture.Second");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Order_Third,       "SimpleQuest.Questline.OrderFixture.Third");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Order_BranchA,     "SimpleQuest.Questline.OrderBranchA");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Order_BranchALeaf, "SimpleQuest.Questline.OrderBranchA.Leaf");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Order_BranchB,     "SimpleQuest.Questline.OrderBranchB");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Order_BranchBLeaf, "SimpleQuest.Questline.OrderBranchB.Leaf");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Order_RootA,       "SimpleQuest.Questline.OrderRootA");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Order_RootB,       "SimpleQuest.Questline.OrderRootB");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Order_Chapter9,    "SimpleQuest.Questline.OrderChapter_9");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Order_Chapter10,   "SimpleQuest.Questline.OrderChapter_10");

namespace
{
	constexpr EAutomationTestFlags OrderTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	/**
	 * A hand-built key table standing in for both of the comparator's real sources - the runtime registry on
	 * UQuestStateSubsystem and the compiler's just-built nodes. FQuestOrdering takes its keys as a callable precisely so
	 * the rule can be exercised without either, which is why none of these tests need a world, a subsystem or a compile.
	 */
	struct FOrderKeyTable
	{
		TMap<FGameplayTag, FQuestOrdering::FQuestOrderingKeys> Keys;

		/** The compiler's walk number - precedence and bias already folded together, which is the point of it. */
		void Position(const FGameplayTag Tag, const int32 InPosition) { Keys.FindOrAdd(Tag).AuthoredPosition = InPosition; }

		void Arrival(const FGameplayTag Tag, const double EntryTime, const int32 Sequence)
		{
			FQuestOrdering::FQuestOrderingKeys& K = Keys.FindOrAdd(Tag);
			K.EntryTime = EntryTime;
			K.EntrySequence = Sequence;
		}

		/** An unknown tag answers with the neutral defaults, exactly as the runtime registry does for one it never saw. */
		FQuestOrdering::FQuestOrderingKeys Get(const FGameplayTag Tag) const
		{
			const FQuestOrdering::FQuestOrderingKeys* Found = Keys.Find(Tag);
			return Found ? *Found : FQuestOrdering::FQuestOrderingKeys();
		}
	};

	/** Drops the shared namespace: a failure message listing six full tag paths is unreadable. */
	FString Describe(const TArray<FGameplayTag>& Tags)
	{
		static const FString Prefix(TEXT("SimpleQuest.Questline."));
		TArray<FString> Parts;
		for (const FGameplayTag& Tag : Tags)
		{
			const FString Full = Tag.ToString();
			Parts.Add(Full.StartsWith(Prefix) ? Full.RightChop(Prefix.Len()) : Full);
		}
		return FString::Join(Parts, TEXT(" > "));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestOrder_AncestorPrecedesDescendant, "SimpleQuest.Ordering.AncestorPrecedesDescendant", OrderTestFlags)
bool FQuestOrder_AncestorPrecedesDescendant::RunTest(const FString& Parameters)
{
	// A parent is presented before its children because the hierarchy says so, and no authored value gets a vote. The
	// QuickStart sidebar depends on this structurally rather than cosmetically: it nests by walking up from each arrival
	// to find a parent already delivered, so a child that lands first is stranded at top level and never re-parented.
	FOrderKeyTable Table;
	Table.Position(TAG_Order_First, 0);         // the lowest position available must still lose to structure
	Table.Position(TAG_Order_Fixture, 500);
	auto Lookup = [&Table](const FGameplayTag Tag) { return Table.Get(Tag); };
	const FQuestOrdering::FOrderingKeyLookup Keys(Lookup);

	TestTrue(TEXT("parent sorts before its child"),           FQuestOrdering::Less(TAG_Order_Fixture, TAG_Order_First, Keys));
	TestFalse(TEXT("child does not sort before its parent"),  FQuestOrdering::Less(TAG_Order_First, TAG_Order_Fixture, Keys));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestOrder_PositionCarriesPrecedenceAndBias, "SimpleQuest.Ordering.PositionCarriesPrecedenceAndBias", OrderTestFlags)
bool FQuestOrder_PositionCarriesPrecedenceAndBias::RunTest(const FString& Parameters)
{
	// Chapter 4's shape, and the numbers the compiler's walk actually produces for it. First, Second and Third are tag
	// siblings, so the hierarchy says nothing about them - but First ACTIVATES the other two, so the depth-first walk
	// numbers it before both; and Third is biased above Second, so the walk visits Third first and numbers it second.
	//
	// Position 0, 1, 2 for First, Third, Second is therefore not an arbitrary fixture - it is the compile output for
	// Third=2 / Second=1, and reading it back gives the "One, Three, Two" the HUD shows.
	FOrderKeyTable Table;
	Table.Position(TAG_Order_First,  0);
	Table.Position(TAG_Order_Third,  1);
	Table.Position(TAG_Order_Second, 2);
	auto Lookup = [&Table](const FGameplayTag Tag) { return Table.Get(Tag); };
	const FQuestOrdering::FOrderingKeyLookup Keys(Lookup);

	TArray<FGameplayTag> Sorted = { TAG_Order_Second, TAG_Order_Third, TAG_Order_First };
	FQuestOrdering::SortTags(Sorted, Keys);

	TestEqual(TEXT("the walk's numbering is reproduced exactly"),
		Describe(Sorted), TEXT("OrderFixture.First > OrderFixture.Third > OrderFixture.Second"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestOrder_KeysReadAtDivergence, "SimpleQuest.Ordering.KeysReadAtDivergence", OrderTestFlags)
bool FQuestOrder_KeysReadAtDivergence::RunTest(const FString& Parameters)
{
	// Two steps in two different chapters are ordered by their CHAPTERS. Nothing authored on a step can reorder the
	// chapters above it, which is what keeps a designer's local change local. BranchA's leaf carries position 0 below -
	// the strongest key in the fixture - purely as sabotage: if the comparison read the leaves it would win, and this
	// expectation would flip.
	FOrderKeyTable Table;
	Table.Position(TAG_Order_BranchA, 50);
	Table.Position(TAG_Order_BranchB, 10);
	Table.Position(TAG_Order_BranchALeaf, 0);
	auto Lookup = [&Table](const FGameplayTag Tag) { return Table.Get(Tag); };
	const FQuestOrdering::FOrderingKeyLookup Keys(Lookup);

	TArray<FGameplayTag> Sorted = { TAG_Order_BranchALeaf, TAG_Order_BranchBLeaf };
	FQuestOrdering::SortTags(Sorted, Keys);

	TestEqual(TEXT("the branches decide, not the leaves"), Describe(Sorted), TEXT("OrderBranchB.Leaf > OrderBranchA.Leaf"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestOrder_ArrivalOrdersRoots, "SimpleQuest.Ordering.ArrivalOrdersRoots", OrderTestFlags)
bool FQuestOrder_ArrivalOrdersRoots::RunTest(const FString& Parameters)
{
	// Two questlines each started on their own. Neither asset refers to the other, so there is no authored order to read
	// and neither has a position; when they began is the only truth available. RootB started first but sorts SECOND by
	// name, so a comparison that ignored arrival would return exactly the opposite of this.
	FOrderKeyTable Table;
	Table.Arrival(TAG_Order_RootA, 20.0, 2);
	Table.Arrival(TAG_Order_RootB, 10.0, 1);
	auto Lookup = [&Table](const FGameplayTag Tag) { return Table.Get(Tag); };
	const FQuestOrdering::FOrderingKeyLookup Keys(Lookup);

	TArray<FGameplayTag> Sorted = { TAG_Order_RootA, TAG_Order_RootB };
	FQuestOrdering::SortTags(Sorted, Keys);

	TestEqual(TEXT("the root that started first is presented first"), Describe(Sorted), TEXT("OrderRootB > OrderRootA"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestOrder_SequenceBreaksTimeTie, "SimpleQuest.Ordering.SequenceBreaksTimeTie", OrderTestFlags)
bool FQuestOrder_SequenceBreaksTimeTie::RunTest(const FString& Parameters)
{
	// The measured case: Chapter 9 and QL_Shortcut both stamped t=12.969375s in frame 195, because the Shortcut's Start
	// Questline node runs inside Chapter 9's graph and the quest clock is sampled per frame. Time cannot separate them,
	// which is the entire reason a sequence is stamped beside it.
	//
	// BOTH directions are asserted. Name order would put RootA first either way, so a comparison that ignored the
	// sequence would pass the first case on a coincidence and only fail the second.
	{
		FOrderKeyTable Table;
		Table.Arrival(TAG_Order_RootA, 12.969375, 8);
		Table.Arrival(TAG_Order_RootB, 12.969375, 7);
		auto Lookup = [&Table](const FGameplayTag Tag) { return Table.Get(Tag); };
		const FQuestOrdering::FOrderingKeyLookup Keys(Lookup);

		TArray<FGameplayTag> Sorted = { TAG_Order_RootA, TAG_Order_RootB };
		FQuestOrdering::SortTags(Sorted, Keys);
		TestEqual(TEXT("lower sequence wins when the clock ties"), Describe(Sorted), TEXT("OrderRootB > OrderRootA"));
	}
	{
		FOrderKeyTable Table;
		Table.Arrival(TAG_Order_RootA, 12.969375, 7);
		Table.Arrival(TAG_Order_RootB, 12.969375, 8);
		auto Lookup = [&Table](const FGameplayTag Tag) { return Table.Get(Tag); };
		const FQuestOrdering::FOrderingKeyLookup Keys(Lookup);

		TArray<FGameplayTag> Sorted = { TAG_Order_RootB, TAG_Order_RootA };
		FQuestOrdering::SortTags(Sorted, Keys);
		TestEqual(TEXT("and the other way round, so it is the sequence deciding and not the name"),
			Describe(Sorted), TEXT("OrderRootA > OrderRootB"));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestOrder_NaturalOrderHandlesChapterNumbers, "SimpleQuest.Ordering.NaturalOrderHandlesChapterNumbers", OrderTestFlags)
bool FQuestOrder_NaturalOrderHandlesChapterNumbers::RunTest(const FString& Parameters)
{
	// Nothing authored, nothing started: the name decides, and it has to decide the way a person reads it. Plain
	// alphabetical puts Chapter_10 ahead of Chapter_9, which is the most visible way a quest log can look broken.
	FOrderKeyTable Table;
	auto Lookup = [&Table](const FGameplayTag Tag) { return Table.Get(Tag); };
	const FQuestOrdering::FOrderingKeyLookup Keys(Lookup);

	TArray<FGameplayTag> Sorted = { TAG_Order_Chapter10, TAG_Order_Chapter9 };
	FQuestOrdering::SortTags(Sorted, Keys);

	TestEqual(TEXT("Chapter_9 precedes Chapter_10"), Describe(Sorted), TEXT("OrderChapter_9 > OrderChapter_10"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestOrder_UnstartedDoesNotCompareOnArrival, "SimpleQuest.Ordering.UnstartedDoesNotCompareOnArrival", OrderTestFlags)
bool FQuestOrder_UnstartedDoesNotCompareOnArrival::RunTest(const FString& Parameters)
{
	// Arrival only speaks when BOTH sides have one. Ordering something that has started against something that has not
	// would reshuffle a list as content comes in, and the outliner lists plenty that has never run. RootA here started
	// late and still comes first, on its name, because RootB has no arrival to compare it against.
	FOrderKeyTable Table;
	Table.Arrival(TAG_Order_RootA, 50.0, 4);
	auto Lookup = [&Table](const FGameplayTag Tag) { return Table.Get(Tag); };
	const FQuestOrdering::FOrderingKeyLookup Keys(Lookup);

	TArray<FGameplayTag> Sorted = { TAG_Order_RootB, TAG_Order_RootA };
	FQuestOrdering::SortTags(Sorted, Keys);

	TestEqual(TEXT("a started root does not outrank an unstarted one"), Describe(Sorted), TEXT("OrderRootA > OrderRootB"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestOrder_SortIsIndependentOfInputOrder, "SimpleQuest.Ordering.SortIsIndependentOfInputOrder", OrderTestFlags)
bool FQuestOrder_SortIsIndependentOfInputOrder::RunTest(const FString& Parameters)
{
	// The property this whole item exists for: the same content must come out the same way whatever order it went in,
	// because the live cascade and a restore hand it to the sort in completely different orders.
	//
	// That only holds if the comparison is a CONSISTENT ordering. If it can ever say A before B, B before C and C
	// before A, the result depends on where the sort happened to start, and no number of correct answers on individual
	// pairs rescues it. Every other test here checks one pair; this one checks that the pairs agree with each other.
	//
	// The set mixes all the rules at once, which is the state a real quest log is in.
	// The triple that caught the old contradiction: First activates Second, Third is a separate root in the same
	// graph, and Third's bias sits between the other two. Under the old rules that produced First before Second (by
	// precedence), Second before Third (by bias), and Third before First (by bias) - three answers that cannot all be
	// true, and three input orders gave three different results. Here it is just three numbers from one walk.
	FOrderKeyTable Table;
	Table.Position(TAG_Order_First,  0);
	Table.Position(TAG_Order_Second, 1);
	Table.Position(TAG_Order_Third,  2);
	Table.Arrival(TAG_Order_RootA, 30.0, 3);
	Table.Arrival(TAG_Order_RootB, 10.0, 1);
	auto Lookup = [&Table](const FGameplayTag Tag) { return Table.Get(Tag); };
	const FQuestOrdering::FOrderingKeyLookup Keys(Lookup);

	const TArray<TArray<FGameplayTag>> Arrangements = {
		{ TAG_Order_Fixture, TAG_Order_First,   TAG_Order_Second,  TAG_Order_Third, TAG_Order_RootA,  TAG_Order_RootB },
		{ TAG_Order_RootB,   TAG_Order_RootA,   TAG_Order_Third,   TAG_Order_Second, TAG_Order_First, TAG_Order_Fixture },
		{ TAG_Order_Third,   TAG_Order_RootA,   TAG_Order_Fixture, TAG_Order_RootB, TAG_Order_First,  TAG_Order_Second },
		{ TAG_Order_Second,  TAG_Order_Fixture, TAG_Order_RootB,   TAG_Order_Third, TAG_Order_First,  TAG_Order_RootA },
	};

	FString Baseline;
	for (int32 Index = 0; Index < Arrangements.Num(); ++Index)
	{
		TArray<FGameplayTag> Sorted = Arrangements[Index];
		FQuestOrdering::SortTags(Sorted, Keys);

		if (Index == 0)
		{
			Baseline = Describe(Sorted);
			continue;
		}
		TestEqual(*FString::Printf(TEXT("arrangement %d sorts the same as the first"), Index), Describe(Sorted), Baseline);
	}

	AddInfo(FString::Printf(TEXT("sorted order: %s"), *Baseline));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestOrder_SiblingBiasBeatsLayout, "SimpleQuest.Ordering.SiblingBiasBeatsLayout", OrderTestFlags)
bool FQuestOrder_SiblingBiasBeatsLayout::RunTest(const FString& Parameters)
{
	// The compile-time half of the rule, and now the ONLY place Order Bias is read. Worth its own coverage because the
	// runtime comparison can no longer catch a mistake here - it just trusts the number the walk produced.
	FQuestOrdering::FAuthoredSiblingKeys Lower;  Lower.OrderBias = 5;  Lower.LayoutY = 900; Lower.LayoutX = 900;
	FQuestOrdering::FAuthoredSiblingKeys Upper;  Upper.OrderBias = 0;  Upper.LayoutY = 0;   Upper.LayoutX = 0;

	TestTrue(TEXT("higher bias wins over a better canvas position"),
		FQuestOrdering::AuthoredSiblingLess(Lower, Upper, TEXT("B_Biased"), TEXT("A_TopLeft")));
	TestFalse(TEXT("and the top-left node does not win on layout"),
		FQuestOrdering::AuthoredSiblingLess(Upper, Lower, TEXT("A_TopLeft"), TEXT("B_Biased")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestOrder_SiblingFallsToLayoutThenName, "SimpleQuest.Ordering.SiblingFallsToLayoutThenName", OrderTestFlags)
bool FQuestOrder_SiblingFallsToLayoutThenName::RunTest(const FString& Parameters)
{
	// With no bias set anywhere - the common case - the canvas decides, top to bottom then left to right. Two nodes
	// stacked exactly on top of each other fall to the name, which is arbitrary but stable, and a node with no editor
	// twin keeps the MAX_int32 defaults and lands at the end.
	FQuestOrdering::FAuthoredSiblingKeys Top;      Top.LayoutY = 10;   Top.LayoutX = 500;
	FQuestOrdering::FAuthoredSiblingKeys Bottom;   Bottom.LayoutY = 90; Bottom.LayoutX = 0;
	FQuestOrdering::FAuthoredSiblingKeys SameSpot; SameSpot.LayoutY = 10; SameSpot.LayoutX = 500;
	const FQuestOrdering::FAuthoredSiblingKeys Unplaced;

	TestTrue(TEXT("higher on the canvas comes first, whatever the X"),
		FQuestOrdering::AuthoredSiblingLess(Top, Bottom, TEXT("Z_Top"), TEXT("A_Bottom")));
	TestTrue(TEXT("exact overlap falls to the name"),
		FQuestOrdering::AuthoredSiblingLess(Top, SameSpot, TEXT("A_First"), TEXT("B_Second")));
	TestTrue(TEXT("a node with no editor twin goes last"),
		FQuestOrdering::AuthoredSiblingLess(Bottom, Unplaced, TEXT("Z_Placed"), TEXT("A_Synthesized")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestOrder_RootsOrderByTheirAncestorsArrival, "SimpleQuest.Ordering.RootsOrderByTheirAncestorsArrival", OrderTestFlags)
bool FQuestOrder_RootsOrderByTheirAncestorsArrival::RunTest(const FString& Parameters)
{
	// *** THE ARRIVAL HAS TO BE ON THE TAG WHERE THE TWO DIVERGE, NOT ON THE CONTENT UNDER IT. *** Two leaves in two
	// different questlines diverge at their ASSET IDENTITY tags, so those are the only keys consulted - stamping the
	// leaves achieves nothing. This shipped broken once for exactly that reason: every RecordEntry call site passed a
	// node's tag, so no identity tag had a row, every key fell through, and root order came out alphabetical.
	//
	// Arrivals below are set ONLY on the parents, and the leaves carry contradictory ones to prove they are ignored.
	FOrderKeyTable Table;
	Table.Arrival(TAG_Order_BranchA, 90.0, 9);          // BranchA's questline started LAST
	Table.Arrival(TAG_Order_BranchB, 10.0, 1);
	Table.Arrival(TAG_Order_BranchALeaf, 0.0, 0);       // ignored - the comparison never reaches a leaf
	auto Lookup = [&Table](const FGameplayTag Tag) { return Table.Get(Tag); };
	const FQuestOrdering::FOrderingKeyLookup Keys(Lookup);

	TArray<FGameplayTag> Sorted = { TAG_Order_BranchALeaf, TAG_Order_BranchBLeaf };
	FQuestOrdering::SortTags(Sorted, Keys);

	TestEqual(TEXT("leaves follow the arrival of the questline they belong to"),
		Describe(Sorted), TEXT("OrderBranchB.Leaf > OrderBranchA.Leaf"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestOrder_LegacyEntriesOrderByTimeAlone, "SimpleQuest.Ordering.LegacyEntriesOrderByTimeAlone", OrderTestFlags)
bool FQuestOrder_LegacyEntriesOrderByTimeAlone::RunTest(const FString& Parameters)
{
	// A save written before EntrySequence existed restores entries with no sequence at all. They tie on that key, so
	// EntryTime has to carry them by itself - which is exactly what it did when those saves were written, so such a
	// save must come back looking the way it looked rather than scrambled.
	//
	// This is the PIE check for old saves, written as a test instead: a legacy entry is INDEX_NONE beside a valid time,
	// and that is a key table, not a save file. Worth pinning because the sentinel normalizing to "sorts last" could
	// just as easily have sent every legacy entry to the end of the list.
	FOrderKeyTable Table;
	Table.Arrival(TAG_Order_RootA, 20.0, INDEX_NONE);
	Table.Arrival(TAG_Order_RootB, 10.0, INDEX_NONE);
	auto Lookup = [&Table](const FGameplayTag Tag) { return Table.Get(Tag); };
	const FQuestOrdering::FOrderingKeyLookup Keys(Lookup);

	TArray<FGameplayTag> Sorted = { TAG_Order_RootA, TAG_Order_RootB };
	FQuestOrdering::SortTags(Sorted, Keys);
	TestEqual(TEXT("time alone orders entries that carry no sequence"), Describe(Sorted), TEXT("OrderRootB > OrderRootA"));

	// And a legacy entry against a post-change one: the sequence still cannot decide it, so time still must.
	FOrderKeyTable Mixed;
	Mixed.Arrival(TAG_Order_RootA, 40.0, 7);
	Mixed.Arrival(TAG_Order_RootB, 30.0, INDEX_NONE);
	auto MixedLookup = [&Mixed](const FGameplayTag Tag) { return Mixed.Get(Tag); };
	const FQuestOrdering::FOrderingKeyLookup MixedKeys(MixedLookup);

	TArray<FGameplayTag> SortedMixed = { TAG_Order_RootA, TAG_Order_RootB };
	FQuestOrdering::SortTags(SortedMixed, MixedKeys);
	TestEqual(TEXT("a sequenced entry does not jump an unsequenced earlier one"),
		Describe(SortedMixed), TEXT("OrderRootB > OrderRootA"));
	return true;
}

#endif

