// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/GameInstance.h"
#include "Misc/AutomationTest.h"
#include "NativeGameplayTags.h"
#include "Subsystems/WorldStateSubsystem.h"
#include "UObject/GCObjectScopeGuard.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_CatchUpFixture_Root,      "SimpleCore.CatchUpFixture");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_CatchUpFixture_Alpha,     "SimpleCore.CatchUpFixture.Alpha");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_CatchUpFixture_Bravo,     "SimpleCore.CatchUpFixture.Bravo");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_CatchUpFixture_Deep,      "SimpleCore.CatchUpFixture.Bravo.Deep");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_CatchUpFixture_Unrelated, "SimpleCore.CatchUpFixtureOther");

namespace
{
	constexpr EAutomationTestFlags CatchUpTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	/**
	 * A WorldState with no subsystem collection behind it, matching the fixture style the SimpleQuest tests use.
	 * The signal subsystem is therefore unreachable, so AddFact records the fact and silently skips its publish - which
	 * is exactly what these tests want, and is stated here so a later reader does not mistake them for coverage of the
	 * DELIVERY half. GetFactsMatching reads nothing but the fact map, so it is fully exercised; SubscribeToFactAdded's
	 * replay-then-wire behavior needs a real collection and is checked in PIE instead.
	 *
	 * The game instance must be the subsystem's outer: AddFact reaches the bus through GetGameInstance(), and a
	 * different outer would make that null.
	 */
	struct FCatchUpFixture
	{
		UGameInstance* GameInstance = nullptr;
		UWorldStateSubsystem* WorldState = nullptr;

		FCatchUpFixture()
		{
			GameInstance = NewObject<UGameInstance>(GetTransientPackage(), UGameInstance::StaticClass());
			WorldState = NewObject<UWorldStateSubsystem>(GameInstance);
		}

		bool IsValid() const { return GameInstance != nullptr && WorldState != nullptr; }
	};

	/** Joins a match list into one string so a failure message names what came back, not just how many. */
	FString Describe(const TArray<FGameplayTag>& Tags)
	{
		FString Out;
		for (const FGameplayTag& Tag : Tags)
		{
			if (!Out.IsEmpty()) Out += TEXT(", ");
			Out += Tag.ToString();
		}
		return Out.IsEmpty() ? TEXT("(none)") : Out;
	}
}


// -------------------------------------------------------------------------------------------------
// Exact routing must not leak descendants. A subscriber that asked for one tag hears one tag live, so
// its catch-up has to match that or the replay says more than the subscription ever would.
// -------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldStateCatchUp_ExactIgnoresDescendants,
	"SimpleCore.WorldState.CatchUp.ExactIgnoresDescendants", CatchUpTestFlags)
bool FWorldStateCatchUp_ExactIgnoresDescendants::RunTest(const FString& Parameters)
{
	FCatchUpFixture Fixture;
	if (!TestTrue(TEXT("Fixture builds"), Fixture.IsValid())) return false;
	FGCObjectScopeGuard InstanceGuard(Fixture.GameInstance);
	FGCObjectScopeGuard WorldStateGuard(Fixture.WorldState);

	Fixture.WorldState->AddFact(TAG_CatchUpFixture_Root);
	Fixture.WorldState->AddFact(TAG_CatchUpFixture_Alpha);
	Fixture.WorldState->AddFact(TAG_CatchUpFixture_Deep);

	TArray<FGameplayTag> Matches;
	Fixture.WorldState->GetFactsMatching(TAG_CatchUpFixture_Root, ESignalRoutingMode::ExactMatch, Matches);

	TestEqual(*FString::Printf(TEXT("exact match returns only the channel itself - got [%s]"), *Describe(Matches)),
		Matches.Num(), 1);
	if (Matches.Num() == 1)
	{
		TestTrue(TEXT("the one match is the channel"), Matches[0] == TAG_CatchUpFixture_Root);
	}
	return true;
}


// -------------------------------------------------------------------------------------------------
// Descendants routing must reach any depth, not just direct children - the bus's own delivery walks the
// whole hierarchy, so a one-level match would quietly under-report on nested tags.
// -------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldStateCatchUp_DescendantsReachAnyDepth,
	"SimpleCore.WorldState.CatchUp.DescendantsReachAnyDepth", CatchUpTestFlags)
bool FWorldStateCatchUp_DescendantsReachAnyDepth::RunTest(const FString& Parameters)
{
	FCatchUpFixture Fixture;
	if (!TestTrue(TEXT("Fixture builds"), Fixture.IsValid())) return false;
	FGCObjectScopeGuard InstanceGuard(Fixture.GameInstance);
	FGCObjectScopeGuard WorldStateGuard(Fixture.WorldState);

	Fixture.WorldState->AddFact(TAG_CatchUpFixture_Root);
	Fixture.WorldState->AddFact(TAG_CatchUpFixture_Alpha);
	Fixture.WorldState->AddFact(TAG_CatchUpFixture_Deep);       // two levels under the channel
	Fixture.WorldState->AddFact(TAG_CatchUpFixture_Unrelated);  // shares a text prefix but is NOT a descendant

	TArray<FGameplayTag> Matches;
	Fixture.WorldState->GetFactsMatching(TAG_CatchUpFixture_Root, ESignalRoutingMode::Descendants, Matches);

	TestEqual(*FString::Printf(TEXT("channel + every descendant at any depth - got [%s]"), *Describe(Matches)),
		Matches.Num(), 3);
	TestTrue(TEXT("includes the channel itself"), Matches.Contains(TAG_CatchUpFixture_Root));
	TestTrue(TEXT("includes the direct child"),   Matches.Contains(TAG_CatchUpFixture_Alpha));
	TestTrue(TEXT("includes the grandchild"),     Matches.Contains(TAG_CatchUpFixture_Deep));
	// "SimpleCore.CatchUpFixtureOther" starts with the channel's string but is a sibling, not a descendant. A
	// string-prefix implementation would wrongly include it; tag matching will not.
	TestFalse(TEXT("excludes the prefix-sharing sibling"), Matches.Contains(TAG_CatchUpFixture_Unrelated));
	return true;
}


// -------------------------------------------------------------------------------------------------
// Replay order has to be stable. TMap iteration order is not, so without the sort this passes by luck for
// a long time and then reorders on an unrelated change - which is the worst possible failure mode.
// -------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldStateCatchUp_ResultsAreSorted,
	"SimpleCore.WorldState.CatchUp.ResultsAreSorted", CatchUpTestFlags)
bool FWorldStateCatchUp_ResultsAreSorted::RunTest(const FString& Parameters)
{
	FCatchUpFixture Fixture;
	if (!TestTrue(TEXT("Fixture builds"), Fixture.IsValid())) return false;
	FGCObjectScopeGuard InstanceGuard(Fixture.GameInstance);
	FGCObjectScopeGuard WorldStateGuard(Fixture.WorldState);

	// Added deliberately out of order so a pass cannot come from insertion order happening to be sorted.
	Fixture.WorldState->AddFact(TAG_CatchUpFixture_Deep);
	Fixture.WorldState->AddFact(TAG_CatchUpFixture_Alpha);
	Fixture.WorldState->AddFact(TAG_CatchUpFixture_Root);
	Fixture.WorldState->AddFact(TAG_CatchUpFixture_Bravo);

	TArray<FGameplayTag> Matches;
	Fixture.WorldState->GetFactsMatching(TAG_CatchUpFixture_Root, ESignalRoutingMode::Descendants, Matches);

	bool bSorted = true;
	for (int32 Index = 1; Index < Matches.Num(); ++Index)
	{
		if (!Matches[Index - 1].GetTagName().LexicalLess(Matches[Index].GetTagName()))
		{
			bSorted = false;
			break;
		}
	}
	TestTrue(*FString::Printf(TEXT("matches come back in lexical order - got [%s]"), *Describe(Matches)), bSorted);
	TestEqual(TEXT("all four are present"), Matches.Num(), 4);
	return true;
}


// -------------------------------------------------------------------------------------------------
// A fact removed back to zero is absent, not present-with-zero. Replaying it would tell a late subscriber
// something is true that is not.
// -------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldStateCatchUp_RemovedFactIsExcluded,
	"SimpleCore.WorldState.CatchUp.RemovedFactIsExcluded", CatchUpTestFlags)
bool FWorldStateCatchUp_RemovedFactIsExcluded::RunTest(const FString& Parameters)
{
	FCatchUpFixture Fixture;
	if (!TestTrue(TEXT("Fixture builds"), Fixture.IsValid())) return false;
	FGCObjectScopeGuard InstanceGuard(Fixture.GameInstance);
	FGCObjectScopeGuard WorldStateGuard(Fixture.WorldState);

	Fixture.WorldState->AddFact(TAG_CatchUpFixture_Alpha);
	Fixture.WorldState->AddFact(TAG_CatchUpFixture_Bravo);
	Fixture.WorldState->RemoveFact(TAG_CatchUpFixture_Alpha);

	TArray<FGameplayTag> Matches;
	Fixture.WorldState->GetFactsMatching(TAG_CatchUpFixture_Root, ESignalRoutingMode::Descendants, Matches);

	TestFalse(*FString::Printf(TEXT("the removed fact is gone - got [%s]"), *Describe(Matches)),
		Matches.Contains(TAG_CatchUpFixture_Alpha));
	TestTrue(TEXT("the surviving fact is still there"), Matches.Contains(TAG_CatchUpFixture_Bravo));
	return true;
}


// -------------------------------------------------------------------------------------------------
// A refcounted fact asserted twice is still ONE thing being true. Catch-up mirrors the 0-to-1 boundary
// event the subscriber would have seen, so it owes exactly one delivery regardless of count.
// -------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldStateCatchUp_CountDoesNotDuplicate,
	"SimpleCore.WorldState.CatchUp.CountDoesNotDuplicate", CatchUpTestFlags)
bool FWorldStateCatchUp_CountDoesNotDuplicate::RunTest(const FString& Parameters)
{
	FCatchUpFixture Fixture;
	if (!TestTrue(TEXT("Fixture builds"), Fixture.IsValid())) return false;
	FGCObjectScopeGuard InstanceGuard(Fixture.GameInstance);
	FGCObjectScopeGuard WorldStateGuard(Fixture.WorldState);

	Fixture.WorldState->AddFact(TAG_CatchUpFixture_Alpha);
	Fixture.WorldState->AddFact(TAG_CatchUpFixture_Alpha);
	Fixture.WorldState->AddFact(TAG_CatchUpFixture_Alpha);
	TestEqual(TEXT("precondition: the fact really is counted three times"),
		Fixture.WorldState->GetFactValue(TAG_CatchUpFixture_Alpha), 3);

	TArray<FGameplayTag> Matches;
	Fixture.WorldState->GetFactsMatching(TAG_CatchUpFixture_Alpha, ESignalRoutingMode::ExactMatch, Matches);

	TestEqual(*FString::Printf(TEXT("one entry, not one per assertion - got [%s]"), *Describe(Matches)),
		Matches.Num(), 1);
	return true;
}


// -------------------------------------------------------------------------------------------------
// Degenerate inputs return empty rather than misbehaving: an invalid channel, and a channel with nothing
// held under it. The out-array is also reset on entry, so a caller reusing one does not accumulate.
// -------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldStateCatchUp_EmptyCasesReturnNothing,
	"SimpleCore.WorldState.CatchUp.EmptyCasesReturnNothing", CatchUpTestFlags)
bool FWorldStateCatchUp_EmptyCasesReturnNothing::RunTest(const FString& Parameters)
{
	FCatchUpFixture Fixture;
	if (!TestTrue(TEXT("Fixture builds"), Fixture.IsValid())) return false;
	FGCObjectScopeGuard InstanceGuard(Fixture.GameInstance);
	FGCObjectScopeGuard WorldStateGuard(Fixture.WorldState);

	Fixture.WorldState->AddFact(TAG_CatchUpFixture_Unrelated);

	// Pre-seeded on purpose: GetFactsMatching must clear what it was handed, not append to it.
	TArray<FGameplayTag> Matches;
	Matches.Add(TAG_CatchUpFixture_Alpha);

	Fixture.WorldState->GetFactsMatching(TAG_CatchUpFixture_Root, ESignalRoutingMode::Descendants, Matches);
	TestEqual(*FString::Printf(TEXT("nothing held under the channel, and the stale entry was cleared - got [%s]"),
		*Describe(Matches)), Matches.Num(), 0);

	Matches.Add(TAG_CatchUpFixture_Alpha);
	Fixture.WorldState->GetFactsMatching(FGameplayTag(), ESignalRoutingMode::Descendants, Matches);
	TestEqual(TEXT("an invalid channel returns empty and does not crash"), Matches.Num(), 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS