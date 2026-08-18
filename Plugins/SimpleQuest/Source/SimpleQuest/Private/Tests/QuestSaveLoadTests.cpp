// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Engine/GameInstance.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/GCObjectScopeGuard.h"
#include "QuickStartSaveGame.h"
#include "Events/QuestGivenEvent.h"
#include "Objectives/CountingQuestObjective.h"
#include "Quests/Types/QuestOutcomeTags.h"
#include "Quests/Types/SimpleQuestSaveSnapshot.h"
#include "Subsystems/QuestStateSubsystem.h"


namespace
{
	constexpr EAutomationTestFlags SaveLoadTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	const FName TestPathIdentity(TEXT("Victory"));
	const FGuid TestStepGuid(0x11111111, 0x22222222, 0x33333333, 0x44444444);

	/**
	 * Populated densely enough to be a real detector: an entry in every map on the root, and a value in every nested
	 * struct a round trip could plausibly drop. The tags are the module's own native tags rather than invented ones,
	 * because FGameplayTag::PostSerialize warns on an unregistered name during load and a warning fails the test.
	 * Which tags they are carries no meaning here - tag identity is not what these tests are about.
	 */
	FSimpleQuestSaveSnapshot MakePopulatedSnapshot(const FGameplayTag& QuestTag, const FGameplayTag& OutcomeTag)
	{
		FSimpleQuestSaveSnapshot Snapshot;
		Snapshot.Version = FSimpleQuestSaveSnapshot::CurrentVersion;
		Snapshot.WorldFacts.Add(QuestTag, 3);
		Snapshot.ActiveGraphs.Add(FSoftObjectPath(TEXT("/Game/Test/QL_Fixture.QL_Fixture")));

		FQuestResolutionEntry Resolution;
		Resolution.OutcomeTag     = OutcomeTag;
		Resolution.PathIdentity   = TestPathIdentity;
		Resolution.ResolutionTime = 1234.56789;   // deliberately NOT exactly representable in float
		Resolution.Source         = EQuestResolutionSource::External;   // non-default, so a dropped enum shows up
		Snapshot.Resolutions.FindOrAdd(QuestTag).History.Add(Resolution);

		FQuestEntryArrival Arrival;
		Arrival.IncomingOutcomeTag = OutcomeTag;
		Arrival.EntryTime          = 9876.54321;   // deliberately NOT exactly representable in float
		Arrival.Provenance         = EQuestActivationProvenance::ChainCascade;
		Arrival.PathIdentity       = TestPathIdentity;
		Arrival.ActivationContextSnapshot.Config.NumElementsRequired = 7;   // two structs deep
		Snapshot.Entries.FindOrAdd(QuestTag).History.Add(Arrival);

		FSimpleQuestObjectiveSaveState ObjectiveState;
		ObjectiveState.bHasState       = true;
		ObjectiveState.CurrentElements = 3;
		ObjectiveState.MaxElements     = 7;
		Snapshot.ObjectiveStates.Add(TestStepGuid, ObjectiveState);

		return Snapshot;
	}
}


/**
 * The snapshot tree survives the engine save path a consumer actually uses, field for field and two structs deep.
 * This is SERIALIZATION FIDELITY, not SaveGame-flag discipline - the flag is not consulted on this path at all, which
 * is what the next test pins down.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimpleQuestSaveLoad_SnapshotRoundTrip,
	"SimpleQuest.SaveLoad.SnapshotRoundTrip", SaveLoadTestFlags)
bool FSimpleQuestSaveLoad_SnapshotRoundTrip::RunTest(const FString& Parameters)
{
	const FGameplayTag QuestTag = Tag_Channel_QuestGiven.GetTag();
	const FGameplayTag OutcomeTag = TAG_Outcome_AnyOutcome.GetTag();

	UQuickStartSaveGame* SaveObject = Cast<UQuickStartSaveGame>(UGameplayStatics::CreateSaveGameObject(UQuickStartSaveGame::StaticClass()));
	TestNotNull(TEXT("save object created"), SaveObject);
	if (!SaveObject) { return false; }
	FGCObjectScopeGuard SaveGuard(SaveObject);
	SaveObject->Snapshot = MakePopulatedSnapshot(QuestTag, OutcomeTag);

	TArray<uint8> Bytes;
	TestTrue(TEXT("SaveGameToMemory succeeded"), UGameplayStatics::SaveGameToMemory(SaveObject, Bytes));
	TestTrue(TEXT("bytes were written"), Bytes.Num() > 0);

	UQuickStartSaveGame* Loaded = Cast<UQuickStartSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes));
	TestNotNull(TEXT("loaded back as the same class"), Loaded);
	if (!Loaded) { return false; }
	FGCObjectScopeGuard LoadGuard(Loaded);

	const FSimpleQuestSaveSnapshot& Out = Loaded->Snapshot;
	TestEqual(TEXT("version"), Out.Version, FSimpleQuestSaveSnapshot::CurrentVersion);
	TestEqual(TEXT("world fact value"), Out.WorldFacts.FindRef(QuestTag), 3);
	TestEqual(TEXT("active graph count"), Out.ActiveGraphs.Num(), 1);

	const FQuestResolutionRecord* Resolution = Out.Resolutions.Find(QuestTag);
	TestNotNull(TEXT("resolution key survived"), Resolution);
	if (Resolution && Resolution->History.Num() == 1)
	{
		const FQuestResolutionEntry& Entry = Resolution->History[0];
		TestTrue (TEXT("resolution outcome tag survived"), Entry.OutcomeTag == OutcomeTag);
		TestTrue (TEXT("resolution path identity survived"), Entry.PathIdentity == TestPathIdentity);
		TestEqual(TEXT("resolution time survived"), static_cast<double>(Entry.ResolutionTime), 1234.56789, 1e-6);
		TestTrue (TEXT("resolution source enum survived"), Entry.Source == EQuestResolutionSource::External);
	}
	else
	{
		AddError(TEXT("expected exactly one resolution entry to survive"));
	}

	const FQuestEntryRecord* EntryRecord = Out.Entries.Find(QuestTag);
	TestNotNull(TEXT("entry key survived"), EntryRecord);
	if (EntryRecord && EntryRecord->History.Num() == 1)
	{
		const FQuestEntryArrival& Arrival = EntryRecord->History[0];
		TestTrue (TEXT("arrival outcome tag survived"), Arrival.IncomingOutcomeTag == OutcomeTag);
		TestEqual(TEXT("arrival time survived"), static_cast<double>(Arrival.EntryTime), 9876.54321, 1e-6);
		TestTrue (TEXT("provenance enum survived"), Arrival.Provenance == EQuestActivationProvenance::ChainCascade);
		// Two structs deep: FQuestEntryArrival -> FQuestObjectiveActivationContext -> FQuestObjectiveAuthoredConfig.
		TestEqual(TEXT("nested activation config survived"), Arrival.ActivationContextSnapshot.Config.NumElementsRequired, 7);
	}
	else
	{
		AddError(TEXT("expected exactly one entry arrival to survive"));
	}

	const FSimpleQuestObjectiveSaveState State = Out.ObjectiveStates.FindRef(TestStepGuid);
	TestEqual(TEXT("objective progress survived under its GUID key"), State.CurrentElements, 3);
	TestEqual(TEXT("objective max survived"), State.MaxElements, 7);
	return true;
}


/**
 * CHARACTERIZATION, NOT ENDORSEMENT. The engine's save path builds its proxy archive without setting ArIsSaveGame, so
 * CPF_SaveGame is never consulted and every non-transient property serializes regardless of the specifier. Two fields
 * are documented as excluded BY omitting that flag, and neither actually is. This test states what the code does today
 * so that changing it - by honouring the flag, or by marking these Transient, or by amending the documentation - is a
 * deliberate act that turns this red rather than a silent drift.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimpleQuestSaveLoad_UnflaggedFieldsStillPersist,
	"SimpleQuest.SaveLoad.UnflaggedFieldsStillPersist", SaveLoadTestFlags)
bool FSimpleQuestSaveLoad_UnflaggedFieldsStillPersist::RunTest(const FString& Parameters)
{
	const FGameplayTag QuestTag = Tag_Channel_QuestGiven.GetTag();
	const FGameplayTag OutcomeTag = TAG_Outcome_AnyOutcome.GetTag();

	UQuickStartSaveGame* SaveObject = Cast<UQuickStartSaveGame>(UGameplayStatics::CreateSaveGameObject(UQuickStartSaveGame::StaticClass()));
	TestNotNull(TEXT("save object created"), SaveObject);
	if (!SaveObject) { return false; }
	FGCObjectScopeGuard SaveGuard(SaveObject);
	SaveObject->Snapshot = MakePopulatedSnapshot(QuestTag, OutcomeTag);

	TArray<uint8> Bytes;
	UGameplayStatics::SaveGameToMemory(SaveObject, Bytes);
	UQuickStartSaveGame* Loaded = Cast<UQuickStartSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes));
	TestNotNull(TEXT("round trip produced an object"), Loaded);
	if (!Loaded) { return false; }
	FGCObjectScopeGuard LoadGuard(Loaded);

	// bHasState carries no SaveGame specifier, yet arrives true. Nothing downstream reads it after load - it is a
	// capture-side filter, consumed before serialization ever happens - so this field is inert either way. It is
	// asserted here only because it is the cheapest available proof that the flag is not consulted on this path.
	TestTrue(TEXT("unflagged bHasState persisted anyway"), Loaded->Snapshot.ObjectiveStates.FindRef(TestStepGuid).bHasState);

	// The Instigator half needs a different kind of evidence. With no real actor to point at, the value round-trips as an
	// empty string whether or not the flag is honoured, so no value assertion can tell those two worlds apart. What does
	// distinguish them is whether the property is in the archive at all - an excluded property emits no tag.
	// The null terminator is load-bearing. Property names are written as strings WITH their terminator, and searching for
	// "Instigator" alone also matches the neighbouring InstigatorRef tag, which is flagged and always present - so the
	// search would succeed regardless of what happened to this field. Comparing the terminator too keeps them distinct.
	auto ArchiveContainsTag = [&Bytes](const ANSICHAR* Name)
	{
		const int32 Len = FCStringAnsi::Strlen(Name) + 1;   // include the terminator the archive writes
		for (int32 Index = 0; Index + Len <= Bytes.Num(); ++Index)
		{
			if (FMemory::Memcmp(Bytes.GetData() + Index, Name, Len) == 0) { return true; }
		}
		return false;
	};

	// Control first: a field that is flagged and unquestionably present, so a failure below means the field is missing
	// rather than the search being broken.
	TestTrue(TEXT("control - the InstigatorRef tag is findable"), ArchiveContainsTag("InstigatorRef"));
	TestTrue(TEXT("the unflagged Instigator property is present in the archive"), ArchiveContainsTag("Instigator"));
	return true;
}


/**
 * ApplySnapshot rebuilds the four query indices that back prerequisite evaluation, and clears them when applying a
 * snapshot that no longer contains the history. The clearing half is the detector: a RebuildRegistryIndices that
 * forgot its Reset() calls would pass every positive assertion and fail only here.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimpleQuestSaveLoad_ApplySnapshotRebuildsIndices,
	"SimpleQuest.SaveLoad.ApplySnapshotRebuildsIndices", SaveLoadTestFlags)
bool FSimpleQuestSaveLoad_ApplySnapshotRebuildsIndices::RunTest(const FString& Parameters)
{
	const FGameplayTag QuestTag = Tag_Channel_QuestGiven.GetTag();
	const FGameplayTag OutcomeTag = TAG_Outcome_AnyOutcome.GetTag();

	// A GameInstance exists here only to satisfy UGameInstanceSubsystem's Within specifier. Init() is deliberately not
	// called: the subsystem collection stays empty, so ApplySnapshot's WorldState lookup finds nothing and its fact
	// restore no-ops. That is why nothing below asserts on WorldFacts - those would not fail, they would never run.
	UGameInstance* GameInstance = NewObject<UGameInstance>(GetTransientPackage(), UGameInstance::StaticClass());
	FGCObjectScopeGuard GameInstanceGuard(GameInstance);
	UQuestStateSubsystem* QuestState = NewObject<UQuestStateSubsystem>(GameInstance);
	FGCObjectScopeGuard QuestStateGuard(QuestState);

	FSimpleQuestSaveSnapshot Snapshot = MakePopulatedSnapshot(QuestTag, OutcomeTag);
	TestTrue(TEXT("ApplySnapshot accepted a current-version snapshot"), QuestState->ApplySnapshot(Snapshot));

	TestTrue(TEXT("ResolvedOutcomesByQuest rebuilt"), QuestState->HasResolvedWith(QuestTag, OutcomeTag));
	TestTrue(TEXT("ResolvedPathsByQuest rebuilt"), QuestState->HasResolvedAtPath(QuestTag, TestPathIdentity));
	TestTrue(TEXT("session-wide ResolvedOutcomes rebuilt"), QuestState->HasAnyQuestResolvedWith(OutcomeTag));
	TestTrue(TEXT("EnteredOutcomesByQuest rebuilt"), QuestState->HasEnteredWith(QuestTag, OutcomeTag));

	// Applying a snapshot without that history must leave no residue - restore REPLACES, it does not accumulate.
	FSimpleQuestSaveSnapshot Empty;
	Empty.Version = FSimpleQuestSaveSnapshot::CurrentVersion;
	TestTrue (TEXT("second ApplySnapshot accepted"), QuestState->ApplySnapshot(Empty));
	TestFalse(TEXT("outcome index cleared"), QuestState->HasResolvedWith(QuestTag, OutcomeTag));
	TestFalse(TEXT("path index cleared"), QuestState->HasResolvedAtPath(QuestTag, TestPathIdentity));
	TestFalse(TEXT("session outcome set cleared"), QuestState->HasAnyQuestResolvedWith(OutcomeTag));
	TestFalse(TEXT("entry index cleared"), QuestState->HasEnteredWith(QuestTag, OutcomeTag));
	return true;
}


/**
 * The counting objective's capture/restore pair, including the clamp against a threshold that shrank since the save.
 * Restore is silent by contract - it re-applies progress directly rather than through SetCurrentElements, so a
 * regression here reports nothing at runtime and simply loses or inflates a player's progress.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimpleQuestSaveLoad_CountingObjectiveCaptureRestore,
	"SimpleQuest.SaveLoad.CountingObjectiveCaptureRestore", SaveLoadTestFlags)
bool FSimpleQuestSaveLoad_CountingObjectiveCaptureRestore::RunTest(const FString& Parameters)
{
	UCountingQuestObjective* Objective = NewObject<UCountingQuestObjective>(GetTransientPackage());
	FGCObjectScopeGuard ObjectiveGuard(Objective);

	FQuestObjectiveAuthoredConfig Authored;
	Authored.NumElementsRequired = 5;
	const FQuestObjectiveRuntimeContext Runtime;   // contributes nothing; MaxElements comes from the authored side

	Objective->DispatchOnObjectiveActivated(Authored, Runtime, FGameplayTag());
	TestEqual(TEXT("MaxElements derived from authored config"), Objective->GetMaxElements(), 5);
	TestEqual(TEXT("progress starts empty"), Objective->GetCurrentElements(), 0);

	Objective->SetCurrentElements(3);
	const FSimpleQuestObjectiveSaveState Captured = Objective->CaptureObjectiveState();
	TestTrue (TEXT("capture marks that there is state"), Captured.bHasState);
	TestEqual(TEXT("captured progress"), Captured.CurrentElements, 3);
	TestEqual(TEXT("captured threshold"), Captured.MaxElements, 5);

	// The real load order: the objective is rebuilt first (which zeroes progress), then restored onto.
	Objective->DispatchOnObjectiveActivated(Authored, Runtime, FGameplayTag());
	TestEqual(TEXT("rebuild zeroed progress"), Objective->GetCurrentElements(), 0);
	Objective->RestoreObjectiveState(Captured);
	TestEqual(TEXT("restore re-applied progress"), Objective->GetCurrentElements(), 3);

	// A designer lowered the requirement after the player saved. Restore clamps to the CURRENT threshold, not the
	// captured one, so a loaded game cannot sit above its own completion bar.
	FQuestObjectiveAuthoredConfig Lowered;
	Lowered.NumElementsRequired = 2;
	Objective->DispatchOnObjectiveActivated(Lowered, Runtime, FGameplayTag());
	Objective->RestoreObjectiveState(Captured);
	TestEqual(TEXT("restore clamped to the lowered threshold"), Objective->GetCurrentElements(), 2);

	// No guard against an empty save state is needed here and none exists: the manager captures only objectives whose
	// class reports durable state, and calls restore only when it finds an entry under that Step's GUID. What the clamp
	// does owe is a floor - a hand-edited or corrupted save carrying a negative count must not drive progress below zero.
	Objective->DispatchOnObjectiveActivated(Authored, Runtime, FGameplayTag());
	FSimpleQuestObjectiveSaveState Negative;
	Negative.bHasState = true;
	Negative.CurrentElements = -5;
	Objective->RestoreObjectiveState(Negative);
	TestEqual(TEXT("restore clamped a negative count up to zero"), Objective->GetCurrentElements(), 0);

	Objective->RestoreObjectiveState(Negative);
	TestEqual(TEXT("restore clamped a negative count up to zero"), Objective->GetCurrentElements(), 0);
	return true;
}

#endif   // WITH_DEV_AUTOMATION_TESTS

