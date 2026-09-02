// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Engine/GameInstance.h"
#include "NativeGameplayTags.h"
#include "Quests/QuestStep.h"
#include "UObject/GCObjectScopeGuard.h"
#include "Quests/Types/QuestAdvancementHold.h"
#include "Settings/SimpleQuestSettings.h"
#include "Subsystems/QuestManagerSubsystem.h"

/**
 * Fixture tags, declared natively in this translation unit rather than drawn from shipped content. Native tags
 * register at static init and never reach the compiled-tag ini, so there is nothing to clean up and no asset for the
 * tests to depend on - a questline being renamed or slimmed cannot break them.
 *
 * The pair is deliberately parent-and-child: ancestry is the property under test.
 */
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_HoldFixture_Container, "SimpleQuest.Questline.HoldFixture");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_HoldFixture_Inner,     "SimpleQuest.Questline.HoldFixture.Inner");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_HoldFixture_Unrelated, "SimpleQuest.Questline.HoldFixtureOther");

/**
 * Friend shim. The hold API is protected, so the tests cannot call it directly; this forwards. Declared as a friend on
 * UQuestManagerSubsystem alongside the PIE debug channel.
 */
class FQuestAdvancementHoldTestAccess
{
public:
	static FQuestAdvancementHold Hold(UQuestManagerSubsystem* M, FGameplayTag Tag, FName Reason, bool bHoldDeactivation = true)
	{
		return M->HoldQuestAdvancement(Tag, Reason, bHoldDeactivation);
	}
	static void Release(UQuestManagerSubsystem* M, const FQuestAdvancementHold& H) { M->ReleaseQuestAdvancement(H); }
	static bool IsHeld(const UQuestManagerSubsystem* M, FGameplayTag Tag)          { return M->IsQuestAdvancementHeld(Tag); }
	static TArray<FName> Reasons(const UQuestManagerSubsystem* M, FGameplayTag Tag){ return M->GetActiveHoldReasons(Tag); }
	static int32 ReleaseAll(UQuestManagerSubsystem* M)                             { return M->ReleaseAllQuestAdvancementHolds(); }

	/**
	 * The gating predicate on its own, with no side effects - provenance rules are tested here rather than through
	 * ActivateNodeByTag, so a "not held" case cannot accidentally run a real activation.
	 */
	static bool ShouldHold(const UQuestManagerSubsystem* M, const UQuestNodeBase* N, FName Tag, EQuestActivationProvenance P, FName SourceTag = NAME_None)
	{
		return M->ShouldHoldActivation(N, Tag, P, SourceTag);
	}

	static void RegisterInstance(UQuestManagerSubsystem* M, FName Tag, UQuestNodeBase* N) { M->LoadedNodeInstances.Add(Tag, N); }
	static void Activate(UQuestManagerSubsystem* M, FName Tag, EQuestActivationProvenance P, FName SourceTag = NAME_None)
	{
		M->ActivateNodeByTag(Tag, P, FGameplayTag(), SourceTag);
	}
	static int32 ParkedCount(const UQuestManagerSubsystem* M)                   { return M->ParkedActivations.Num(); }
	static void WarnOlderThan(UQuestManagerSubsystem* M, double Now)            { M->WarnOnHoldsOlderThan(Now); }
	static FName ParkedTagAt(const UQuestManagerSubsystem* M, int32 Index)      { return M->ParkedActivations[Index].NodeTagName; }
};

namespace
{
	constexpr EAutomationTestFlags HoldTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	/**
	 * A manager with no subsystem collection behind it. WorldState is therefore null, so the Held FACT writes no-op -
	 * which is fine and is stated here so a later reader does not mistake these for coverage of the fact side. The
	 * registry, the matching, and the reason reporting all run normally; only the replication-visible half is absent.
	 */
	struct FHoldFixture
	{
		UGameInstance* GameInstance = nullptr;
		UQuestManagerSubsystem* Manager = nullptr;

		FHoldFixture()
		{
			GameInstance = NewObject<UGameInstance>(GetTransientPackage(), UGameInstance::StaticClass());
			Manager = NewObject<UQuestManagerSubsystem>(GameInstance);
		}

		bool IsValid() const { return GameInstance != nullptr && Manager != nullptr; }
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestHold_ComposesUntilLastRelease, "SimpleQuest.Hold.ComposesUntilLastRelease", HoldTestFlags)
bool FQuestHold_ComposesUntilLastRelease::RunTest(const FString& Parameters)
{
	// Two systems pausing the same quest must not cancel each other. An audio hold and a cutscene hold know nothing
	// about one another, so advancement resumes only when the LAST one clears - which a single flag cannot express.
	FHoldFixture Fixture;
	if (!TestTrue(TEXT("Fixture builds"), Fixture.IsValid())) return false;
	FGCObjectScopeGuard InstanceGuard(Fixture.GameInstance);
	FGCObjectScopeGuard ManagerGuard(Fixture.Manager);

	const FQuestAdvancementHold Audio    = FQuestAdvancementHoldTestAccess::Hold(Fixture.Manager, TAG_HoldFixture_Container, TEXT("Audio"));
	const FQuestAdvancementHold Cutscene = FQuestAdvancementHoldTestAccess::Hold(Fixture.Manager, TAG_HoldFixture_Container, TEXT("Cutscene"));

	TestTrue(TEXT("first hold issued a valid handle"),  Audio.IsValid());
	TestTrue(TEXT("second hold issued a valid handle"), Cutscene.IsValid());
	TestTrue(TEXT("handles are distinct"),              Audio.Id != Cutscene.Id);
	TestTrue(TEXT("held while both are active"),        FQuestAdvancementHoldTestAccess::IsHeld(Fixture.Manager, TAG_HoldFixture_Container));

	FQuestAdvancementHoldTestAccess::Release(Fixture.Manager, Audio);
	TestTrue(TEXT("STILL held after only the first released"), FQuestAdvancementHoldTestAccess::IsHeld(Fixture.Manager, TAG_HoldFixture_Container));

	FQuestAdvancementHoldTestAccess::Release(Fixture.Manager, Cutscene);
	TestFalse(TEXT("released once the last hold cleared"), FQuestAdvancementHoldTestAccess::IsHeld(Fixture.Manager, TAG_HoldFixture_Container));

	// Releasing twice must be harmless: a holder that cannot tell whether it already released should be able to just
	// call this rather than tracking state the manager already tracks.
	FQuestAdvancementHoldTestAccess::Release(Fixture.Manager, Cutscene);
	TestFalse(TEXT("double release left nothing held"), FQuestAdvancementHoldTestAccess::IsHeld(Fixture.Manager, TAG_HoldFixture_Container));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestHold_ReachesDescendantsNotSiblings, "SimpleQuest.Hold.ReachesDescendantsNotSiblings", HoldTestFlags)
bool FQuestHold_ReachesDescendantsNotSiblings::RunTest(const FString& Parameters)
{
	// *** THE SCOPING TEST. *** Holding a container must hold everything inside it and nothing outside it. Both halves
	// matter and they fail differently: reaching too little means a cutscene does not actually pause the quest it is
	// pacing; reaching too far means one questline's cutscene silently freezes an unrelated one, which is the failure
	// a player would report as "the game stopped."
	//
	// MUST BE SHOWN RED WITH ANCESTRY MATCHING REMOVED. Swap MatchesTag for == in NodeMatchesHoldTag and the
	// descendant assertion below must fail. A version of this that passes either way proves nothing.
	FHoldFixture Fixture;
	if (!TestTrue(TEXT("Fixture builds"), Fixture.IsValid())) return false;
	FGCObjectScopeGuard InstanceGuard(Fixture.GameInstance);
	FGCObjectScopeGuard ManagerGuard(Fixture.Manager);

	const FQuestAdvancementHold Held = FQuestAdvancementHoldTestAccess::Hold(Fixture.Manager, TAG_HoldFixture_Container, TEXT("Cutscene"));
	TestTrue(TEXT("hold issued"), Held.IsValid());

	TestTrue (TEXT("the held container itself is held"),      FQuestAdvancementHoldTestAccess::IsHeld(Fixture.Manager, TAG_HoldFixture_Container));
	TestTrue (TEXT("a node INSIDE the container is held"),    FQuestAdvancementHoldTestAccess::IsHeld(Fixture.Manager, TAG_HoldFixture_Inner));
	TestFalse(TEXT("an unrelated questline is NOT held"),     FQuestAdvancementHoldTestAccess::IsHeld(Fixture.Manager, TAG_HoldFixture_Unrelated));

	// Ancestry runs one way only. Holding the inner node must not pause the container above it - a beat after one step
	// is not a reason to freeze the questline that contains it.
	FQuestAdvancementHoldTestAccess::Release(Fixture.Manager, Held);
	const FQuestAdvancementHold InnerHold = FQuestAdvancementHoldTestAccess::Hold(Fixture.Manager, TAG_HoldFixture_Inner, TEXT("Beat"));
	TestTrue (TEXT("the inner node is held"),                          FQuestAdvancementHoldTestAccess::IsHeld(Fixture.Manager, TAG_HoldFixture_Inner));
	TestFalse(TEXT("its CONTAINER is not held by an inner-node hold"), FQuestAdvancementHoldTestAccess::IsHeld(Fixture.Manager, TAG_HoldFixture_Container));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestHold_ReportsEveryReachingReason, "SimpleQuest.Hold.ReportsEveryReachingReason", HoldTestFlags)
bool FQuestHold_ReportsEveryReachingReason::RunTest(const FString& Parameters)
{
	// Reason is required rather than optional because a hold nobody releases is otherwise undiagnosable - the quest
	// simply stops and nothing says why. This asserts the diagnosis actually works, including for a node held from
	// above, where the reason lives on a tag the caller never asked about.
	FHoldFixture Fixture;
	if (!TestTrue(TEXT("Fixture builds"), Fixture.IsValid())) return false;
	FGCObjectScopeGuard InstanceGuard(Fixture.GameInstance);
	FGCObjectScopeGuard ManagerGuard(Fixture.Manager);

	FQuestAdvancementHoldTestAccess::Hold(Fixture.Manager, TAG_HoldFixture_Container, TEXT("Cutscene"));
	FQuestAdvancementHoldTestAccess::Hold(Fixture.Manager, TAG_HoldFixture_Inner,     TEXT("Audio"));

	const TArray<FName> InnerReasons = FQuestAdvancementHoldTestAccess::Reasons(Fixture.Manager, TAG_HoldFixture_Inner);
	TestEqual(TEXT("inner node reports both reasons reaching it"), InnerReasons.Num(), 2);
	TestTrue (TEXT("reports the hold placed on its container"),    InnerReasons.Contains(FName(TEXT("Cutscene"))));
	TestTrue (TEXT("reports the hold placed on itself"),           InnerReasons.Contains(FName(TEXT("Audio"))));

	const TArray<FName> ContainerReasons = FQuestAdvancementHoldTestAccess::Reasons(Fixture.Manager, TAG_HoldFixture_Container);
	TestEqual(TEXT("container reports only the hold on itself"), ContainerReasons.Num(), 1);
	TestTrue (TEXT("and it is the right one"),                   ContainerReasons.Contains(FName(TEXT("Cutscene"))));

	TestEqual(TEXT("an unheld questline reports no reasons"),
		FQuestAdvancementHoldTestAccess::Reasons(Fixture.Manager, TAG_HoldFixture_Unrelated).Num(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestHold_ReleaseAllDropsEveryHold, "SimpleQuest.Hold.ReleaseAllDropsEveryHold", HoldTestFlags)
bool FQuestHold_ReleaseAllDropsEveryHold::RunTest(const FString& Parameters)
{
	// The drain runs before a save snapshot is captured, and it is what keeps held state out of saves entirely. If it
	// under-reports or leaves a hold behind, a save taken mid-pause restores a game that looks stuck.
	FHoldFixture Fixture;
	if (!TestTrue(TEXT("Fixture builds"), Fixture.IsValid())) return false;
	FGCObjectScopeGuard InstanceGuard(Fixture.GameInstance);
	FGCObjectScopeGuard ManagerGuard(Fixture.Manager);

	FQuestAdvancementHoldTestAccess::Hold(Fixture.Manager, TAG_HoldFixture_Container, TEXT("Cutscene"));
	FQuestAdvancementHoldTestAccess::Hold(Fixture.Manager, TAG_HoldFixture_Container, TEXT("Audio"));
	FQuestAdvancementHoldTestAccess::Hold(Fixture.Manager, TAG_HoldFixture_Unrelated, TEXT("Elsewhere"));

	TestEqual(TEXT("drain reported every hold it dropped"), FQuestAdvancementHoldTestAccess::ReleaseAll(Fixture.Manager), 3);

	TestFalse(TEXT("container no longer held"), FQuestAdvancementHoldTestAccess::IsHeld(Fixture.Manager, TAG_HoldFixture_Container));
	TestFalse(TEXT("inner node no longer held"), FQuestAdvancementHoldTestAccess::IsHeld(Fixture.Manager, TAG_HoldFixture_Inner));
	TestFalse(TEXT("unrelated questline no longer held"), FQuestAdvancementHoldTestAccess::IsHeld(Fixture.Manager, TAG_HoldFixture_Unrelated));

	TestEqual(TEXT("draining again drops nothing"), FQuestAdvancementHoldTestAccess::ReleaseAll(Fixture.Manager), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestHold_GatesOnlyCascadeProvenance, "SimpleQuest.Hold.GatesOnlyCascadeProvenance", HoldTestFlags)
bool FQuestHold_GatesOnlyCascadeProvenance::RunTest(const FString& Parameters)
{
	// A hold pauses ADVANCEMENT, not input. A player accepting a quest from a giver, an explicit activation request
	// from game code, or a graph starting up are all deliberate acts from outside the chain, and pacing the chain is
	// not a reason to refuse them. Only cascade provenances are gated.
	//
	// Tested through the predicate rather than through ActivateNodeByTag: the "not held" cases would otherwise run a
	// real activation, so a bug in this test would show up as unrelated damage somewhere else.
	FHoldFixture Fixture;
	if (!TestTrue(TEXT("Fixture builds"), Fixture.IsValid())) return false;
	FGCObjectScopeGuard InstanceGuard(Fixture.GameInstance);
	FGCObjectScopeGuard ManagerGuard(Fixture.Manager);

	const FName InnerName = TAG_HoldFixture_Inner.GetTag().GetTagName();
	FQuestAdvancementHoldTestAccess::Hold(Fixture.Manager, TAG_HoldFixture_Container, TEXT("Cutscene"));

	using EProv = EQuestActivationProvenance;
	TestTrue (TEXT("ChainCascade is held"),        FQuestAdvancementHoldTestAccess::ShouldHold(Fixture.Manager, nullptr, InnerName, EProv::ChainCascade, InnerName));
	TestTrue (TEXT("DeactivationCascade is held by default"),
	                                              FQuestAdvancementHoldTestAccess::ShouldHold(Fixture.Manager, nullptr, InnerName, EProv::DeactivationCascade, InnerName));
	TestFalse(TEXT("GiverGate is NOT held"),      FQuestAdvancementHoldTestAccess::ShouldHold(Fixture.Manager, nullptr, InnerName, EProv::GiverGate, InnerName));
	TestFalse(TEXT("ExternalAPI is NOT held"),    FQuestAdvancementHoldTestAccess::ShouldHold(Fixture.Manager, nullptr, InnerName, EProv::ExternalAPI, InnerName));
	TestFalse(TEXT("InitialEntry is NOT held"),   FQuestAdvancementHoldTestAccess::ShouldHold(Fixture.Manager, nullptr, InnerName, EProv::InitialEntry, InnerName));
	TestFalse(TEXT("Restored is NOT held"),       FQuestAdvancementHoldTestAccess::ShouldHold(Fixture.Manager, nullptr, InnerName, EProv::Restored, InnerName));

	// The deactivation opt-out. A caller whose deactivation routes are corrective cleanup can let them proceed while
	// forward progress waits - so a hold placed with it off must gate the one and not the other.
	FQuestAdvancementHoldTestAccess::ReleaseAll(Fixture.Manager);
	FQuestAdvancementHoldTestAccess::Hold(Fixture.Manager, TAG_HoldFixture_Container, TEXT("Cutscene"), /*bHoldDeactivation=*/false);

	TestTrue (TEXT("forward cascade still held with the opt-out set"),
	          FQuestAdvancementHoldTestAccess::ShouldHold(Fixture.Manager, nullptr, InnerName, EProv::ChainCascade, InnerName));
	TestFalse(TEXT("deactivation cascade allowed through by the opt-out"),
	          FQuestAdvancementHoldTestAccess::ShouldHold(Fixture.Manager, nullptr, InnerName, EProv::DeactivationCascade, InnerName));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestHold_ParksCascadeInArrivalOrder, "SimpleQuest.Hold.ParksCascadeInArrivalOrder", HoldTestFlags)
bool FQuestHold_ParksCascadeInArrivalOrder::RunTest(const FString& Parameters)
{
	// Parking is the whole mechanism: an activation that arrives while held must be stored VERBATIM and replayed in
	// the order it arrived, or a questline resumes with its steps out of sequence. The check runs immediately after
	// the instance lookup and returns, so nothing on the destination node is touched - which is what makes a replayed
	// activation indistinguishable from one that was never held.
	FHoldFixture Fixture;
	if (!TestTrue(TEXT("Fixture builds"), Fixture.IsValid())) return false;
	FGCObjectScopeGuard InstanceGuard(Fixture.GameInstance);
	FGCObjectScopeGuard ManagerGuard(Fixture.Manager);

	// ActivateNodeByTag needs a registered instance to get as far as the hold check. It needs nothing else: the check
	// returns before the activation guard, so a bare Step with no objective is sufficient and stays untouched.
	UQuestStep* Inner = NewObject<UQuestStep>(Fixture.Manager);
	UQuestStep* Other = NewObject<UQuestStep>(Fixture.Manager);
	FGCObjectScopeGuard InnerGuard(Inner);
	FGCObjectScopeGuard OtherGuard(Other);

	const FName InnerName     = TAG_HoldFixture_Inner.GetTag().GetTagName();
	const FName ContainerName = TAG_HoldFixture_Container.GetTag().GetTagName();
	FQuestAdvancementHoldTestAccess::RegisterInstance(Fixture.Manager, InnerName, Inner);
	FQuestAdvancementHoldTestAccess::RegisterInstance(Fixture.Manager, ContainerName, Other);

	const FQuestAdvancementHold Cutscene = FQuestAdvancementHoldTestAccess::Hold(Fixture.Manager, TAG_HoldFixture_Container, TEXT("Cutscene"));
	const FQuestAdvancementHold Audio    = FQuestAdvancementHoldTestAccess::Hold(Fixture.Manager, TAG_HoldFixture_Container, TEXT("Audio"));

	TestEqual(TEXT("nothing parked before any activation"), FQuestAdvancementHoldTestAccess::ParkedCount(Fixture.Manager), 0);

	// The sequence is deliberately NOT a palindrome. Inner/Container/Inner reads the same backwards, so a queue built
	// in reverse would satisfy every assertion below and the ordering claim would be unfalsifiable.
	// SOURCE is what a hold matches. A hold names the node whose downstream flow is paused, so these park because
	// they were CAUSED BY Inner - which sits under the held container - not because of where they are going.
	FQuestAdvancementHoldTestAccess::Activate(Fixture.Manager, InnerName,     EQuestActivationProvenance::ChainCascade, InnerName);
	FQuestAdvancementHoldTestAccess::Activate(Fixture.Manager, ContainerName, EQuestActivationProvenance::ChainCascade, InnerName);
	FQuestAdvancementHoldTestAccess::Activate(Fixture.Manager, ContainerName, EQuestActivationProvenance::ChainCascade, InnerName);

	// Guard before indexing. A test that dies instead of failing takes every other result with it, and this one has
	// already done that once.
	if (!TestEqual(TEXT("all three activations parked"), FQuestAdvancementHoldTestAccess::ParkedCount(Fixture.Manager), 3))
	{
		return false;
	}
	TestEqual(TEXT("arrival order preserved — first"),  FQuestAdvancementHoldTestAccess::ParkedTagAt(Fixture.Manager, 0), InnerName);
	TestEqual(TEXT("arrival order preserved — second"), FQuestAdvancementHoldTestAccess::ParkedTagAt(Fixture.Manager, 1), ContainerName);
	TestEqual(TEXT("arrival order preserved — third"),  FQuestAdvancementHoldTestAccess::ParkedTagAt(Fixture.Manager, 2), ContainerName);

	// NOTHING IS RELEASED HERE, AND THAT IS DELIBERATE. Releasing lets ReplayParkedActivations run for real, and a
	// real activation needs a world this fixture does not build - so instead of failing, the test would CRASH the
	// editor and take every other result with it. That is unacceptable in a suite whose guards are routinely verified
	// by deliberately breaking them: the run has to survive the thing it is trying to catch.
	// Re-parking on a partial release, and resuming on a full one, belong with the cascade-level harness. See the
	// runtime-harness item.
	return true;
}

/**
 * A hold nobody released eventually says so - once.
 *
 * The ONCE is the real assertion, and it is why the expectation is pinned to exactly one occurrence: the check runs on
 * a repeating timer, so a warning that fired every pass would bury the very thing it exists to surface. Calling it
 * twice and still expecting one message is what proves the latch, and a broken latch fails on the count rather than
 * quietly producing noise nobody reads.
 *
 * No world here, so a held record's PlacedAtSeconds is zero; handing the check a later "now" is the same arithmetic
 * the timer performs, without needing time to pass.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestHold_AbandonedHoldWarnsOnce, "SimpleQuest.Hold.AbandonedHoldWarnsOnce", HoldTestFlags)
bool FQuestHold_AbandonedHoldWarnsOnce::RunTest(const FString& Parameters)
{
	AddExpectedMessagePlain(TEXT("has been active for"), ELogVerbosity::Warning,
		EAutomationExpectedMessageFlags::Contains, 1);

	FHoldFixture Fixture;
	if (!TestTrue(TEXT("Fixture builds"), Fixture.IsValid())) return false;
	FGCObjectScopeGuard InstanceGuard(Fixture.GameInstance);
	FGCObjectScopeGuard ManagerGuard(Fixture.Manager);

	// Set rather than assumed: the shipped default is a product decision that can move, and a test that silently
	// depends on it stops testing what it claims the day somebody tunes it.
	USimpleQuestSettings* Settings = GetMutableDefault<USimpleQuestSettings>();
	const float Original = Settings->AbandonedHoldWarningSeconds;
	Settings->AbandonedHoldWarningSeconds = 60.f;
	ON_SCOPE_EXIT { Settings->AbandonedHoldWarningSeconds = Original; };

	FQuestAdvancementHoldTestAccess::Hold(Fixture.Manager, TAG_HoldFixture_Container, TEXT("AbandonedAudio"));

	FQuestAdvancementHoldTestAccess::WarnOlderThan(Fixture.Manager, 120.0);
	FQuestAdvancementHoldTestAccess::WarnOlderThan(Fixture.Manager, 240.0);

	// Still held: warning is a report, never a release. Auto-releasing would hide the bug behind a questline that
	// mostly works, which is the whole reason this is a log line rather than a fix.
	TestTrue(TEXT("the hold is still in force after warning"),
		FQuestAdvancementHoldTestAccess::IsHeld(Fixture.Manager, TAG_HoldFixture_Container));

	return true;
}

#endif

