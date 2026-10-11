// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/GameInstance.h"
#include "Misc/AutomationTest.h"
#include "NativeGameplayTags.h"
#include "Components/QuestObserverComponent.h"
#include "Events/QuestStartedEvent.h"
#include "Subsystems/QuestStateSubsystem.h"
#include "Subsystems/SignalSubsystem.h"
#include "Subsystems/WorldStateSubsystem.h"
#include "Tests/QuestObserverDedupeTestTypes.h"
#include "UObject/GCObjectScopeGuard.h"
#include "Utilities/QuestTagComposer.h"

/**
 * A parent and two children, plus the lifecycle facts a reconstruction reads. The state tags have to be declared too:
 * catch-up asks the tag manager for them by name, and an unregistered name comes back invalid.
 */
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_ObsFixture_Root,          "SimpleQuest.Questline.ObsFixture");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_ObsFixture_Child,         "SimpleQuest.Questline.ObsFixture.Child");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_ObsFixture_Sibling,       "SimpleQuest.Questline.ObsFixture.Sibling");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_ObsFixture_ChildStarted,  "SimpleQuest.State.ObsFixture.Child.Started");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_ObsFixture_ChildLive,     "SimpleQuest.State.ObsFixture.Child.Live");

/**
 * Friend shim. Registration and the observed map are protected, and KnownQuests is manager-only, so the fixture
 * reaches both through here rather than standing up a world, a manager, and a compiled graph.
 */
class FQuestObserverDedupeTestAccess
{
public:
	static void SetBus(UQuestObserverComponent* Component, USignalSubsystem* Signals) { Component->SignalSubsystem = Signals; }
	static void SetObserved(UQuestObserverComponent* Component, const TMap<FGameplayTag, FObservedQuestEventSettings>& Tags)
	{
		Component->ObservedTags = Tags;
	}
	static void Register(UQuestObserverComponent* Component) { Component->RegisterQuestObserver(); }

	/** Catch-up reads the subsystems from the owning world, which a bare NewObject'd component has none of, so the
	 *  fixture hands them in directly. SubscriptionHandlesByTag is seeded because the fan-out treats its absence as
	 *  "this tag was unsubscribed mid-pass" and bails. */
	static void CatchUp(UQuestObserverComponent* Component, const FGameplayTag Tag, const FObservedQuestEventSettings& Settings,
		UWorldStateSubsystem* WorldState, UQuestStateSubsystem* QuestState, TSet<FGameplayTag>* Pass)
	{
		Component->SubscriptionHandlesByTag.FindOrAdd(Tag);
		Component->CatchUpSingleTag(Tag, Settings, WorldState, QuestState, Pass);
	}

	static void KnowTag(UQuestStateSubsystem* QuestState, const FGameplayTag Tag) { QuestState->RegisterQuestTag(Tag, true); }
};

namespace
{
	constexpr EAutomationTestFlags ObsTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	struct FObsFixture
	{
		UGameInstance* GameInstance = nullptr;
		USignalSubsystem* Signals = nullptr;
		UWorldStateSubsystem* WorldState = nullptr;
		UQuestStateSubsystem* QuestState = nullptr;
		UQuestObserverComponent* Observer = nullptr;
		UQuestObserverDedupeSink* Sink = nullptr;

		FObsFixture()
		{
			GameInstance = NewObject<UGameInstance>(GetTransientPackage(), UGameInstance::StaticClass());
			Signals      = NewObject<USignalSubsystem>(GameInstance);
			WorldState   = NewObject<UWorldStateSubsystem>(GameInstance);
			QuestState   = NewObject<UQuestStateSubsystem>(GameInstance);
			Observer     = NewObject<UQuestObserverComponent>(GameInstance);
			Sink         = NewObject<UQuestObserverDedupeSink>(GameInstance);

			FQuestObserverDedupeTestAccess::SetBus(Observer, Signals);
			Observer->OnAnyQuestEvent.AddDynamic(Sink, &UQuestObserverDedupeSink::OnAny);
		}

		bool IsValid() const { return GameInstance && Signals && WorldState && QuestState && Observer && Sink; }

		/** An ancestor that hears descendants, and one of those descendants listed in its own right. */
		static TMap<FGameplayTag, FObservedQuestEventSettings> OverlappingPair()
		{
			FObservedQuestEventSettings Ancestor;
			Ancestor.Routing = FSignalRoutingDefaults::HierarchicalSubscribe;
			Ancestor.bObserveStarted = true;

			FObservedQuestEventSettings Descendant;
			Descendant.Routing = FSignalRoutingDefaults::ExactOnly;
			Descendant.bObserveStarted = true;

			return { { TAG_ObsFixture_Root, Ancestor }, { TAG_ObsFixture_Child, Descendant } };
		}
	};
}

// -------------------------------------------------------------------------------------------------
// The console's bug, at component level: one actor watching a questline AND a Step inside it.
// -------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestObserverDedupe_OverlappingTagsDeliverOnceLive,
	"SimpleQuest.Observer.Dedupe.OverlappingTagsDeliverOnceLive", ObsTestFlags)
bool FQuestObserverDedupe_OverlappingTagsDeliverOnceLive::RunTest(const FString& Parameters)
{
	FObsFixture Fixture;
	if (!TestTrue(TEXT("Fixture builds"), Fixture.IsValid())) return false;
	FGCObjectScopeGuard Guard(Fixture.GameInstance);

	FQuestObserverDedupeTestAccess::SetObserved(Fixture.Observer, FObsFixture::OverlappingPair());
	FQuestObserverDedupeTestAccess::Register(Fixture.Observer);

	FQuestEventPayload Payload;
	Payload.NodeInfo.QuestTag = TAG_ObsFixture_Child;
	Fixture.Signals->PublishMessage<FQuestStartedEvent>(TAG_ObsFixture_Child,
		FQuestStartedEvent(TAG_ObsFixture_Child, Payload, nullptr));

	TestEqual(TEXT("the Step's STARTED is reported once, not once per matching subscription"),
		Fixture.Sink->CountFor(TAG_ObsFixture_Child, EQuestLifecycleEventType::Started), 1);
	return true;
}

// -------------------------------------------------------------------------------------------------
// The half the bus cannot fix: catch-up is a direct call, and the ancestor's fan-out covers the
// descendant the next entry is about to replay on its own.
// -------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestObserverDedupe_CatchUpReportsEachTagOnce,
	"SimpleQuest.Observer.Dedupe.CatchUpReportsEachTagOnce", ObsTestFlags)
bool FQuestObserverDedupe_CatchUpReportsEachTagOnce::RunTest(const FString& Parameters)
{
	FObsFixture Fixture;
	if (!TestTrue(TEXT("Fixture builds"), Fixture.IsValid())) return false;
	FGCObjectScopeGuard Guard(Fixture.GameInstance);

	// A Step that has started, known to the registry so the ancestor's fan-out can find it.
	FQuestObserverDedupeTestAccess::KnowTag(Fixture.QuestState, TAG_ObsFixture_Root);
	FQuestObserverDedupeTestAccess::KnowTag(Fixture.QuestState, TAG_ObsFixture_Child);
	Fixture.WorldState->AddFact(TAG_ObsFixture_ChildStarted);
	Fixture.WorldState->AddFact(TAG_ObsFixture_ChildLive);

	// The registration pass, as RegisterQuestObserver runs it: most specific first, one shared seen-set.
	const TMap<FGameplayTag, FObservedQuestEventSettings> Observed = FObsFixture::OverlappingPair();
	TSet<FGameplayTag> Pass;
	FQuestObserverDedupeTestAccess::CatchUp(Fixture.Observer, TAG_ObsFixture_Child, Observed[TAG_ObsFixture_Child],
		Fixture.WorldState, Fixture.QuestState, &Pass);
	FQuestObserverDedupeTestAccess::CatchUp(Fixture.Observer, TAG_ObsFixture_Root, Observed[TAG_ObsFixture_Root],
		Fixture.WorldState, Fixture.QuestState, &Pass);

	TestEqual(TEXT("the Step's STARTED is replayed once across the whole pass"),
		Fixture.Sink->CountFor(TAG_ObsFixture_Child, EQuestLifecycleEventType::Started), 1);
	return true;
}

// -------------------------------------------------------------------------------------------------
// *** THE SABOTAGE CHECK. *** Deduplicating by anything coarser - per pass, per component, per event
// type - would pass both tests above and silence a second, unrelated tag. Two tags that do not
// overlap must both be reported.
// -------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestObserverDedupe_SeparateTagsBothReport,
	"SimpleQuest.Observer.Dedupe.SeparateTagsBothReport", ObsTestFlags)
bool FQuestObserverDedupe_SeparateTagsBothReport::RunTest(const FString& Parameters)
{
	FObsFixture Fixture;
	if (!TestTrue(TEXT("Fixture builds"), Fixture.IsValid())) return false;
	FGCObjectScopeGuard Guard(Fixture.GameInstance);

	FObservedQuestEventSettings Exact;
	Exact.Routing = FSignalRoutingDefaults::ExactOnly;
	Exact.bObserveStarted = true;

	FQuestObserverDedupeTestAccess::SetObserved(Fixture.Observer,
		{ { TAG_ObsFixture_Child, Exact }, { TAG_ObsFixture_Sibling, Exact } });
	FQuestObserverDedupeTestAccess::Register(Fixture.Observer);

	FQuestEventPayload Payload;
	Fixture.Signals->PublishMessage<FQuestStartedEvent>(TAG_ObsFixture_Child,
		FQuestStartedEvent(TAG_ObsFixture_Child, Payload, nullptr));
	Fixture.Signals->PublishMessage<FQuestStartedEvent>(TAG_ObsFixture_Sibling,
		FQuestStartedEvent(TAG_ObsFixture_Sibling, Payload, nullptr));

	TestEqual(TEXT("the first tag reported"),  Fixture.Sink->CountFor(TAG_ObsFixture_Child,   EQuestLifecycleEventType::Started), 1);
	TestEqual(TEXT("the second tag reported"), Fixture.Sink->CountFor(TAG_ObsFixture_Sibling, EQuestLifecycleEventType::Started), 1);
	return true;
}

#endif

