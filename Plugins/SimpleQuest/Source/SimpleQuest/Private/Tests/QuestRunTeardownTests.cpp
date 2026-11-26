// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Engine/GameInstance.h"
#include "GameFramework/Actor.h"
#include "Utilities/ScopedFixtureGameplayTags.h"
#include "Quests/QuestStep.h"
#include "UObject/GCObjectScopeGuard.h"
#include "Events/QuestDeactivatedEvent.h"
#include "Quests/Types/QuestAdvancementHold.h"
#include "Subsystems/QuestManagerSubsystem.h"

/**
 * Fixture tags, registered per test like the hold tests' (see FHoldFixtureTags for why). Container and Inner are parent and
 * child because a hold matches by ancestry; Other sits outside both.
 */
namespace
{
	FGameplayTag TAG_TeardownFixture_Container;
	FGameplayTag TAG_TeardownFixture_Inner;
	FGameplayTag TAG_TeardownFixture_Other;

	struct FTeardownFixtureTags : FScopedFixtureGameplayTags
	{
		FTeardownFixtureTags() : FScopedFixtureGameplayTags(UE_PLUGIN_NAME, UE_MODULE_NAME, {
			{ TAG_TeardownFixture_Container, TEXT("SimpleQuest.Questline.TeardownFixture") },
			{ TAG_TeardownFixture_Inner,     TEXT("SimpleQuest.Questline.TeardownFixture.Inner") },
			{ TAG_TeardownFixture_Other,     TEXT("SimpleQuest.Questline.TeardownFixtureOther") } })
		{
		}
	};
}

/** Friend shim, declared on UQuestManagerSubsystem: the teardown is private and the hold API it exercises is protected. */
class FQuestRunTeardownTestAccess
{
public:
	static UQuestManagerSubsystem::FQuestRunTeardownTally TearDown(UQuestManagerSubsystem* M, const TArray<UQuestNodeBase*>& Instances, bool bPublish)
	{
		return M->TearDownRun(Instances, TArray<FGameplayTag>(), bPublish);
	}
	static void RegisterInstance(UQuestManagerSubsystem* M, FName Tag, UQuestNodeBase* N) { M->LoadedNodeInstances.Add(Tag, N); }
	static FQuestAdvancementHold Hold(UQuestManagerSubsystem* M, FGameplayTag Tag, FName Reason) { return M->HoldQuestAdvancement(Tag, Reason); }
	static bool IsHeld(const UQuestManagerSubsystem* M, FGameplayTag Tag) { return M->IsQuestAdvancementHeld(Tag); }
	static void Activate(UQuestManagerSubsystem* M, FName Tag, FName SourceTag)
	{
		M->ActivateNodeByTag(Tag, EQuestActivationProvenance::ChainCascade, FGameplayTag(), SourceTag);
	}
	static int32 ParkedCount(const UQuestManagerSubsystem* M) { return M->ParkedActivations.Num(); }
	static FName ParkedTagAt(const UQuestManagerSubsystem* M, int32 Index) { return M->ParkedActivations[Index].NodeTagName; }
	static void AddClassFilter(UQuestManagerSubsystem* M, FGameplayTag StepTag, UClass* Class) { M->ClassFilteredSteps.Add(StepTag, Class); }
	static int32 ClassFilterCount(const UQuestManagerSubsystem* M, FGameplayTag StepTag) { return M->ClassFilteredSteps.Num(StepTag); }
	static void Unwire(UQuestManagerSubsystem* M, FGameplayTag StepTag) { M->UnwireStepTriggerSubscriptions(StepTag); }
	static void AddDeactivatedRoute(UQuestNodeBase* N, FName Target) { N->NextNodesOnDeactivation.Add(Target); }
	static void DeliverDeactivated(UQuestManagerSubsystem* M, FGameplayTag Tag, EDeactivationSource Source)
	{
		M->HandleNodeDeactivatedEvent(Tag, FQuestDeactivatedEvent(Tag, Source));
	}
};

namespace
{
	constexpr EAutomationTestFlags TeardownTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	/**
	 * The hold tests' fixture: a manager with no subsystem collection, so WorldState and the signal bus are null. Every fact
	 * write and publish in the teardown is therefore a no-op - stated here so nobody reads these as coverage of the lifecycle
	 * half (the RunEnded events, the cleared facts, the disarm). What runs for real is the manager's own bookkeeping: parked
	 * activations, holds, and trigger filters.
	 */
	struct FTeardownFixture
	{
		FTeardownFixtureTags Tags;   // first, so the tags exist before anything else in the fixture is built
		UGameInstance* GameInstance = nullptr;
		UQuestManagerSubsystem* Manager = nullptr;

		FTeardownFixture()
		{
			GameInstance = NewObject<UGameInstance>(GetTransientPackage(), UGameInstance::StaticClass());
			Manager = NewObject<UQuestManagerSubsystem>(GameInstance);
		}

		bool IsValid() const { return GameInstance != nullptr && Manager != nullptr; }
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestRunTeardown_DropsOnlyTheRunsParkedActivations, "SimpleQuest.RunTeardown.DropsOnlyTheRunsParkedActivations", TeardownTestFlags)
bool FQuestRunTeardown_DropsOnlyTheRunsParkedActivations::RunTest(const FString& Parameters)
{
	// A parked activation aimed at the run would be replayed INTO it the moment a hold drops, so the teardown has to remove
	// it - and only it. Activations parked for content outside the run belong to another run.
	FTeardownFixture Fixture;
	if (!TestTrue(TEXT("Fixture builds"), Fixture.IsValid())) return false;
	FGCObjectScopeGuard InstanceGuard(Fixture.GameInstance);
	FGCObjectScopeGuard ManagerGuard(Fixture.Manager);

	UQuestStep* InRun = NewObject<UQuestStep>(Fixture.Manager);
	UQuestStep* OutsideRun = NewObject<UQuestStep>(Fixture.Manager);
	FGCObjectScopeGuard InRunGuard(InRun);
	FGCObjectScopeGuard OutsideGuard(OutsideRun);

	const FName InnerName     = TAG_TeardownFixture_Inner.GetTagName();
	const FName ContainerName = TAG_TeardownFixture_Container.GetTagName();
	FQuestRunTeardownTestAccess::RegisterInstance(Fixture.Manager, InnerName, InRun);
	FQuestRunTeardownTestAccess::RegisterInstance(Fixture.Manager, ContainerName, OutsideRun);

	// The hold sits on the CONTAINER, which is not part of the run being ended, so the teardown never drops it - and so never
	// replays the queue, which this fixture could not survive (a real activation needs a world).
	FQuestRunTeardownTestAccess::Hold(Fixture.Manager, TAG_TeardownFixture_Container, TEXT("Cutscene"));

	// All three are caused by Inner, under the held container. The one aimed at the run sits in the MIDDLE, so a teardown
	// that dropped from either end of the queue would leave the wrong two.
	FQuestRunTeardownTestAccess::Activate(Fixture.Manager, ContainerName, InnerName);
	FQuestRunTeardownTestAccess::Activate(Fixture.Manager, InnerName,     InnerName);
	FQuestRunTeardownTestAccess::Activate(Fixture.Manager, ContainerName, InnerName);
	if (!TestEqual(TEXT("all three activations parked"), FQuestRunTeardownTestAccess::ParkedCount(Fixture.Manager), 3)) return false;

	const auto Tally = FQuestRunTeardownTestAccess::TearDown(Fixture.Manager, TArray<UQuestNodeBase*>{ InRun }, true);

	TestEqual(TEXT("the tally counts one dropped activation"), Tally.DroppedParked, 1);
	if (!TestEqual(TEXT("two activations still parked"), FQuestRunTeardownTestAccess::ParkedCount(Fixture.Manager), 2)) return false;
	TestEqual(TEXT("the first survivor is aimed outside the run"),  FQuestRunTeardownTestAccess::ParkedTagAt(Fixture.Manager, 0), ContainerName);
	TestEqual(TEXT("the second survivor is aimed outside the run"), FQuestRunTeardownTestAccess::ParkedTagAt(Fixture.Manager, 1), ContainerName);
	TestTrue(TEXT("the hold outside the run is still in force"), FQuestRunTeardownTestAccess::IsHeld(Fixture.Manager, TAG_TeardownFixture_Container));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestRunTeardown_DropsHoldsOnTheRunsTags,	"SimpleQuest.RunTeardown.DropsHoldsOnTheRunsTags", TeardownTestFlags)
bool FQuestRunTeardown_DropsHoldsOnTheRunsTags::RunTest(const FString& Parameters)
{
	// A hold names content whose run is ending. Left in place it would pause the NEXT run's first cascade for a reason nobody
	// remembers; dropped, it says so once - the same Warning a completion gives.
	AddExpectedMessagePlain(TEXT("went away while hold"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);

	FTeardownFixture Fixture;
	if (!TestTrue(TEXT("Fixture builds"), Fixture.IsValid())) return false;
	FGCObjectScopeGuard InstanceGuard(Fixture.GameInstance);
	FGCObjectScopeGuard ManagerGuard(Fixture.Manager);

	UQuestStep* InRun = NewObject<UQuestStep>(Fixture.Manager);
	FGCObjectScopeGuard InRunGuard(InRun);
	InRun->ResolveContextualTag(TAG_TeardownFixture_Inner.GetTagName());   // the run's holds are found through its tags
	FQuestRunTeardownTestAccess::RegisterInstance(Fixture.Manager, TAG_TeardownFixture_Inner.GetTagName(), InRun);

	FQuestRunTeardownTestAccess::Hold(Fixture.Manager, TAG_TeardownFixture_Inner, TEXT("Audio"));
	FQuestRunTeardownTestAccess::Hold(Fixture.Manager, TAG_TeardownFixture_Other, TEXT("Elsewhere"));

	const auto Tally = FQuestRunTeardownTestAccess::TearDown(Fixture.Manager, TArray<UQuestNodeBase*>{ InRun }, true);

	TestEqual(TEXT("the tally counts one dropped hold"), Tally.DroppedHolds, 1);
	TestFalse(TEXT("the run's hold is gone"), FQuestRunTeardownTestAccess::IsHeld(Fixture.Manager, TAG_TeardownFixture_Inner));
	TestTrue(TEXT("a hold outside the run survives"), FQuestRunTeardownTestAccess::IsHeld(Fixture.Manager, TAG_TeardownFixture_Other));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestRunTeardown_UnwireDropsEveryClassFilter,	"SimpleQuest.RunTeardown.UnwireDropsEveryClassFilter", TeardownTestFlags)
bool FQuestRunTeardown_UnwireDropsEveryClassFilter::RunTest(const FString& Parameters)
{
	// Class filters used to be added on every activation and removed by nothing, so a Step that ran twice forwarded each
	// matching trigger twice. Two entries on one Step is exactly that state: unwiring has to clear BOTH and leave other Steps
	// alone. (The other half of the fix, AddUnique at wire time, needs a signal bus this fixture lacks; the PIE recipe covers it.)
	FTeardownFixture Fixture;
	if (!TestTrue(TEXT("Fixture builds"), Fixture.IsValid())) return false;
	FGCObjectScopeGuard InstanceGuard(Fixture.GameInstance);
	FGCObjectScopeGuard ManagerGuard(Fixture.Manager);

	FQuestRunTeardownTestAccess::AddClassFilter(Fixture.Manager, TAG_TeardownFixture_Inner, AActor::StaticClass());
	FQuestRunTeardownTestAccess::AddClassFilter(Fixture.Manager, TAG_TeardownFixture_Inner, AActor::StaticClass());
	FQuestRunTeardownTestAccess::AddClassFilter(Fixture.Manager, TAG_TeardownFixture_Other, AActor::StaticClass());
	if (!TestEqual(TEXT("the duplicate pair is really there"),
		FQuestRunTeardownTestAccess::ClassFilterCount(Fixture.Manager, TAG_TeardownFixture_Inner), 2)) return false;

	FQuestRunTeardownTestAccess::Unwire(Fixture.Manager, TAG_TeardownFixture_Inner);

	TestEqual(TEXT("both filters on the unwired Step are gone"), FQuestRunTeardownTestAccess::ClassFilterCount(Fixture.Manager, TAG_TeardownFixture_Inner), 0);
	TestEqual(TEXT("another Step's filter is untouched"), FQuestRunTeardownTestAccess::ClassFilterCount(Fixture.Manager, TAG_TeardownFixture_Other), 1);
	return true;
}

/**
 * The promise the verb makes: a run ending fires nothing wired to a Deactivated output. The route here points at a tag with no
 * instance, so following it logs a Warning and stops, which is what lets this fixture survive it and what the test counts.
 * The Internal delivery is the control: it proves the route really fires, so a count of exactly ONE can only hold if the
 * RunEnded delivery stayed silent. A broken guard fails on the count.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestRunTeardown_RunEndedFiresNoDeactivatedRoutes, "SimpleQuest.RunTeardown.RunEndedFiresNoDeactivatedRoutes", TeardownTestFlags)
bool FQuestRunTeardown_RunEndedFiresNoDeactivatedRoutes::RunTest(const FString& Parameters)
{
	AddExpectedMessagePlain(TEXT("no instance found for tag name"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);

	FTeardownFixture Fixture;
	if (!TestTrue(TEXT("Fixture builds"), Fixture.IsValid())) return false;
	FGCObjectScopeGuard InstanceGuard(Fixture.GameInstance);
	FGCObjectScopeGuard ManagerGuard(Fixture.Manager);

	UQuestStep* Step = NewObject<UQuestStep>(Fixture.Manager);
	FGCObjectScopeGuard StepGuard(Step);
	FQuestRunTeardownTestAccess::RegisterInstance(Fixture.Manager, TAG_TeardownFixture_Inner.GetTagName(), Step);
	FQuestRunTeardownTestAccess::AddDeactivatedRoute(Step, TAG_TeardownFixture_Other.GetTagName());

	FQuestRunTeardownTestAccess::DeliverDeactivated(Fixture.Manager, TAG_TeardownFixture_Inner, EDeactivationSource::Internal);
	FQuestRunTeardownTestAccess::DeliverDeactivated(Fixture.Manager, TAG_TeardownFixture_Inner, EDeactivationSource::RunEnded);
	return true;
}

#endif

