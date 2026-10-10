// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/GameInstance.h"
#include "Misc/AutomationTest.h"
#include "NativeGameplayTags.h"
#include "Subsystems/SignalSubsystem.h"
#include "Tests/SignalDedupeTestListener.h"
#include "UObject/GCObjectScopeGuard.h"

/** A parent and two children, so one subscriber can hold an ancestor subscription and a descendant one at once. */
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_DedupeFixture_Root,  "SimpleCore.DedupeFixture");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_DedupeFixture_Child, "SimpleCore.DedupeFixture.Child");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_DedupeFixture_Other, "SimpleCore.DedupeFixture.Other");

/**
 * OUTSIDE that subtree, for background traffic that must reach nothing under test. The three tags above are
 * all heard by a hierarchical subscription on the root - that is what they exist for - which makes every one
 * of them useless as "unrelated".
 */
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_DedupeBystander,     "SimpleCore.DedupeBystander");

namespace
{
	constexpr EAutomationTestFlags DedupeTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	/**
	 * A bus with no subsystem collection behind it, matching the fixture style of the other SimpleCore and SimpleQuest
	 * tests. Nothing here needs the collection: subscribe and publish are called straight on the subsystem.
	 */
	struct FDedupeFixture
	{
		UGameInstance* GameInstance = nullptr;
		USignalSubsystem* Signals = nullptr;
		USignalDedupeTestListener* Listener = nullptr;

		FDedupeFixture()
		{
			GameInstance = NewObject<UGameInstance>(GetTransientPackage(), UGameInstance::StaticClass());
			Signals = NewObject<USignalSubsystem>(GameInstance);
			Listener = NewObject<USignalDedupeTestListener>(GameInstance);
		}

		bool IsValid() const { return GameInstance && Signals && Listener; }
	};

	/**
	 * Overwrites the stack a shallow call would be using. Handler identity is built from a pointer-to-member, and on
	 * MSVC that can be wider than a code address with trailing bytes nobody initialized - so two copies of the SAME
	 * handler can key differently depending on what was underneath them. Taking both keys back to back in one frame
	 * reuses the same bytes and agrees with itself, which proves nothing. These helpers put real distance between the
	 * two, matching what an adopter meets when one subscription comes from registration and the next from a runtime
	 * Add Observed Tag seconds later.
	 */
	FORCENOINLINE void DirtyTheStack()
	{
		volatile uint8 Scratch[512];
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Scratch); ++Index) { Scratch[Index] = static_cast<uint8>(0xA5 ^ Index); }
	}

	/** Takes the key one frame deeper than the caller, over stack this helper has just scribbled on. */
	FORCENOINLINE uint64 KeyFromADeeperFrame(USignalDedupeTestListener* Listener)
	{
		DirtyTheStack();
		return USignalSubsystem::MakeHandlerKey(Listener, &USignalDedupeTestListener::HandlerA);
	}

	/** Subscribes from one frame deeper, for the same reason. */
	FORCENOINLINE void SubscribeFromADeeperFrame(USignalSubsystem* Signals, USignalDedupeTestListener* Listener, FGameplayTag Tag)
	{
		DirtyTheStack();
		Signals->SubscribeMessage<FSignalDedupeTestEvent>(Tag, Listener, &USignalDedupeTestListener::HandlerA,
			FSignalRoutingDefaults::ExactOnly);
	}
}

// -------------------------------------------------------------------------------------------------
// The key itself, with no bus involved. Everything below depends on these three properties holding, so
// they are worth pinning separately - a failure here says the key is wrong, not that delivery is.
// -------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSignalDedupe_KeyDiscriminates,
	"SimpleCore.Signal.Dedupe.KeyDiscriminates", DedupeTestFlags)
bool FSignalDedupe_KeyDiscriminates::RunTest(const FString& Parameters)
{
	FDedupeFixture Fixture;
	if (!TestTrue(TEXT("Fixture builds"), Fixture.IsValid())) return false;
	FGCObjectScopeGuard InstanceGuard(Fixture.GameInstance);
	FGCObjectScopeGuard ListenerGuard(Fixture.Listener);

	USignalDedupeTestListener* Other = NewObject<USignalDedupeTestListener>(Fixture.GameInstance);
	FGCObjectScopeGuard OtherGuard(Other);

	const uint64 A1 = USignalSubsystem::MakeHandlerKey(Fixture.Listener, &USignalDedupeTestListener::HandlerA);
	const uint64 A2 = USignalSubsystem::MakeHandlerKey(Fixture.Listener, &USignalDedupeTestListener::HandlerA);
	const uint64 B  = USignalSubsystem::MakeHandlerKey(Fixture.Listener, &USignalDedupeTestListener::HandlerB);
	const uint64 OtherA = USignalSubsystem::MakeHandlerKey(Other, &USignalDedupeTestListener::HandlerA);

	TestEqual(TEXT("the same object and the same function key identically"), A1, A2);
	TestNotEqual(TEXT("two different functions on one object are two handlers"), A1, B);
	TestNotEqual(TEXT("the same function on two objects is two handlers"), A1, OtherA);
	TestNotEqual(TEXT("a key is never zero, which is reserved for 'unkeyed'"), A1, static_cast<uint64>(0));

	return true;
}

// -------------------------------------------------------------------------------------------------
// The defect this whole change exists for, on the SINGLE-CHANNEL path - the one most publishes take.
// A component watching a questline with Descendants AND a Step inside it is matched at two levels of
// one ancestor walk, and used to be called at both.
// -------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSignalDedupe_OverlappingSubscriptionsDeliverOnce,
	"SimpleCore.Signal.Dedupe.OverlappingSubscriptionsDeliverOnce", DedupeTestFlags)
bool FSignalDedupe_OverlappingSubscriptionsDeliverOnce::RunTest(const FString& Parameters)
{
	FDedupeFixture Fixture;
	if (!TestTrue(TEXT("Fixture builds"), Fixture.IsValid())) return false;
	FGCObjectScopeGuard InstanceGuard(Fixture.GameInstance);
	FGCObjectScopeGuard ListenerGuard(Fixture.Listener);

	// One handler, two overlapping subscriptions: the ancestor hears descendants, the child hears only itself.
	Fixture.Signals->SubscribeMessage<FSignalDedupeTestEvent>(TAG_DedupeFixture_Root, Fixture.Listener,
		&USignalDedupeTestListener::HandlerA, FSignalRoutingDefaults::HierarchicalSubscribe);
	Fixture.Signals->SubscribeMessage<FSignalDedupeTestEvent>(TAG_DedupeFixture_Child, Fixture.Listener,
		&USignalDedupeTestListener::HandlerA, FSignalRoutingDefaults::ExactOnly);

	Fixture.Signals->PublishMessage<FSignalDedupeTestEvent>(TAG_DedupeFixture_Child, FSignalDedupeTestEvent());

	TestEqual(TEXT("one subscriber, one delivery, however many subscriptions matched"), Fixture.Listener->CallsA, 1);
	return true;
}

// -------------------------------------------------------------------------------------------------
// *** THE SABOTAGE CHECK. *** Collapsing the key to the listener alone would make the test above pass
// and this one fail. It is not hypothetical: UQuestTriggerComponent deliberately binds its own role
// handlers on the same tags its inherited observer surface watches, and both are meant to fire.
// -------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSignalDedupe_DistinctHandlersBothDeliver,
	"SimpleCore.Signal.Dedupe.DistinctHandlersBothDeliver", DedupeTestFlags)
bool FSignalDedupe_DistinctHandlersBothDeliver::RunTest(const FString& Parameters)
{
	FDedupeFixture Fixture;
	if (!TestTrue(TEXT("Fixture builds"), Fixture.IsValid())) return false;
	FGCObjectScopeGuard InstanceGuard(Fixture.GameInstance);
	FGCObjectScopeGuard ListenerGuard(Fixture.Listener);

	Fixture.Signals->SubscribeMessage<FSignalDedupeTestEvent>(TAG_DedupeFixture_Root, Fixture.Listener,
		&USignalDedupeTestListener::HandlerA, FSignalRoutingDefaults::HierarchicalSubscribe);
	Fixture.Signals->SubscribeMessage<FSignalDedupeTestEvent>(TAG_DedupeFixture_Child, Fixture.Listener,
		&USignalDedupeTestListener::HandlerB, FSignalRoutingDefaults::ExactOnly);

	Fixture.Signals->PublishMessage<FSignalDedupeTestEvent>(TAG_DedupeFixture_Child, FSignalDedupeTestEvent());

	TestEqual(TEXT("the ancestor handler fired"), Fixture.Listener->CallsA, 1);
	TestEqual(TEXT("the second, different handler fired too - two handlers are not a duplicate"), Fixture.Listener->CallsB, 1);
	return true;
}

// -------------------------------------------------------------------------------------------------
// The multichannel walk is a separate code path from the single-channel one and needs its own proof.
// -------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSignalDedupe_MultiChannelPublishDeliversOnce,
	"SimpleCore.Signal.Dedupe.MultiChannelPublishDeliversOnce", DedupeTestFlags)
bool FSignalDedupe_MultiChannelPublishDeliversOnce::RunTest(const FString& Parameters)
{
	FDedupeFixture Fixture;
	if (!TestTrue(TEXT("Fixture builds"), Fixture.IsValid())) return false;
	FGCObjectScopeGuard InstanceGuard(Fixture.GameInstance);
	FGCObjectScopeGuard ListenerGuard(Fixture.Listener);

	Fixture.Signals->SubscribeMessage<FSignalDedupeTestEvent>(TAG_DedupeFixture_Root, Fixture.Listener,
		&USignalDedupeTestListener::HandlerA, FSignalRoutingDefaults::HierarchicalSubscribe);
	Fixture.Signals->SubscribeMessage<FSignalDedupeTestEvent>(TAG_DedupeFixture_Child, Fixture.Listener,
		&USignalDedupeTestListener::HandlerA, FSignalRoutingDefaults::ExactOnly);

	// Two channels, both of which reach the ancestor subscription, plus one that also hits the child's.
	const TArray<FGameplayTag> Channels = { TAG_DedupeFixture_Child, TAG_DedupeFixture_Other };
	Fixture.Signals->PublishMessageOnChannels<FSignalDedupeTestEvent>(Channels, FSignalDedupeTestEvent());

	TestEqual(TEXT("one delivery across the whole channel set"), Fixture.Listener->CallsA, 1);
	return true;
}

// -------------------------------------------------------------------------------------------------
// bAllChannels is the documented opt-out - "fire once per channel as a naive sibling-publish would."
// Pinned so that nobody later reads the duplicates as the same bug and deduplicates the opt-out away.
// -------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSignalDedupe_AllChannelsOptsOutOfDedupe,
	"SimpleCore.Signal.Dedupe.AllChannelsOptsOutOfDedupe", DedupeTestFlags)
bool FSignalDedupe_AllChannelsOptsOutOfDedupe::RunTest(const FString& Parameters)
{
	FDedupeFixture Fixture;
	if (!TestTrue(TEXT("Fixture builds"), Fixture.IsValid())) return false;
	FGCObjectScopeGuard InstanceGuard(Fixture.GameInstance);
	FGCObjectScopeGuard ListenerGuard(Fixture.Listener);

	Fixture.Signals->SubscribeMessage<FSignalDedupeTestEvent>(TAG_DedupeFixture_Root, Fixture.Listener,
		&USignalDedupeTestListener::HandlerA, FSignalRoutingDefaults::HierarchicalSubscribe);

	const TArray<FGameplayTag> Channels = { TAG_DedupeFixture_Child, TAG_DedupeFixture_Other };
	Fixture.Signals->PublishMessageOnChannels<FSignalDedupeTestEvent>(Channels, FSignalDedupeTestEvent(), /*bAllChannels*/ true);

	TestEqual(TEXT("the opt-out fires once per channel, deliberately"), Fixture.Listener->CallsA, 2);
	return true;
}

// -------------------------------------------------------------------------------------------------
// The routing filter has to survive the new key. An Exact subscriber must still hear nothing when the
// publish lands on its ANCESTOR - the key decides who is a duplicate, never who is eligible.
// -------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSignalDedupe_ExactStillIgnoresAncestorPublish,
	"SimpleCore.Signal.Dedupe.ExactStillIgnoresAncestorPublish", DedupeTestFlags)
bool FSignalDedupe_ExactStillIgnoresAncestorPublish::RunTest(const FString& Parameters)
{
	FDedupeFixture Fixture;
	if (!TestTrue(TEXT("Fixture builds"), Fixture.IsValid())) return false;
	FGCObjectScopeGuard InstanceGuard(Fixture.GameInstance);
	FGCObjectScopeGuard ListenerGuard(Fixture.Listener);

	Fixture.Signals->SubscribeMessage<FSignalDedupeTestEvent>(TAG_DedupeFixture_Child, Fixture.Listener,
		&USignalDedupeTestListener::HandlerA, FSignalRoutingDefaults::ExactOnly);

	Fixture.Signals->PublishMessage<FSignalDedupeTestEvent>(TAG_DedupeFixture_Root, FSignalDedupeTestEvent());

	TestEqual(TEXT("an exact subscriber hears nothing from a publish on its ancestor"), Fixture.Listener->CallsA, 0);
	return true;
}

// -------------------------------------------------------------------------------------------------
// *** THE REGRESSION THAT MATTERS, AND THE ONE THIS FILE ORIGINALLY MISSED. *** Handler identity has
// to survive TIME, not merely differ by argument. Every test above binds its two subscriptions back to
// back inside one frame, which is precisely the arrangement that let an unstable key pass as stable.
//
// The first test below asserts the property and is the one to trust. The second asserts the behavior
// and is a belt - stack contents are not a test's to control, so it CAN pass with the defect present.
// It cannot pass with the defect present while the first test is also green.
// -------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSignalDedupe_KeyIsStableAcrossFrames,
	"SimpleCore.Signal.Dedupe.KeyIsStableAcrossFrames", DedupeTestFlags)
bool FSignalDedupe_KeyIsStableAcrossFrames::RunTest(const FString& Parameters)
{
	FDedupeFixture Fixture;
	if (!TestTrue(TEXT("Fixture builds"), Fixture.IsValid())) return false;
	FGCObjectScopeGuard InstanceGuard(Fixture.GameInstance);
	FGCObjectScopeGuard ListenerGuard(Fixture.Listener);

	const uint64 Early = USignalSubsystem::MakeHandlerKey(Fixture.Listener, &USignalDedupeTestListener::HandlerA);
	DirtyTheStack();
	const uint64 Later = KeyFromADeeperFrame(Fixture.Listener);

	TestEqual(TEXT("one handler keys identically however and whenever the key is taken"), Early, Later);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSignalDedupe_SubscriptionsSeparatedInTimeDeliverOnce,
	"SimpleCore.Signal.Dedupe.SubscriptionsSeparatedInTimeDeliverOnce", DedupeTestFlags)
bool FSignalDedupe_SubscriptionsSeparatedInTimeDeliverOnce::RunTest(const FString& Parameters)
{
	FDedupeFixture Fixture;
	if (!TestTrue(TEXT("Fixture builds"), Fixture.IsValid())) return false;
	FGCObjectScopeGuard InstanceGuard(Fixture.GameInstance);
	FGCObjectScopeGuard ListenerGuard(Fixture.Listener);

	// The authored subscription, as a component takes it at registration.
	Fixture.Signals->SubscribeMessage<FSignalDedupeTestEvent>(TAG_DedupeFixture_Root, Fixture.Listener,
		&USignalDedupeTestListener::HandlerA, FSignalRoutingDefaults::HierarchicalSubscribe);

	// Unrelated work in between: another subscriber arrives, events flow, the stack moves on. The tag has to sit
	// OUTSIDE the fixture subtree - everything under the root is heard by the hierarchical subscription taken
	// above, so a sibling tag here scores a legitimate delivery and the count at the end reads it as a repeat.
	USignalDedupeTestListener* Bystander = NewObject<USignalDedupeTestListener>(Fixture.GameInstance);
	FGCObjectScopeGuard BystanderGuard(Bystander);
	Fixture.Signals->SubscribeMessage<FSignalDedupeTestEvent>(TAG_DedupeBystander, Bystander,
		&USignalDedupeTestListener::HandlerB, FSignalRoutingDefaults::ExactOnly);
	Fixture.Signals->PublishMessage<FSignalDedupeTestEvent>(TAG_DedupeBystander, FSignalDedupeTestEvent());
	DirtyTheStack();

	// Pins the mistake this test was first written with, so the count at the end can only mean what it says.
	TestEqual(TEXT("the background traffic reached nothing under test"), Fixture.Listener->CallsA, 0);

	// The runtime subscription, as Add Observed Tag takes it much later. Overlaps the first one on Child.
	SubscribeFromADeeperFrame(Fixture.Signals, Fixture.Listener, TAG_DedupeFixture_Child);

	Fixture.Signals->PublishMessage<FSignalDedupeTestEvent>(TAG_DedupeFixture_Child, FSignalDedupeTestEvent());

	TestEqual(TEXT("one delivery, though the two subscriptions were taken at different moments"), Fixture.Listener->CallsA, 1);
	TestEqual(TEXT("the bystander's unrelated handler was undisturbed"), Bystander->CallsB, 1);
	return true;
}

#endif

