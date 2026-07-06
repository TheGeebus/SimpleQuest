// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT


#include "SimpleQuestSaveLoadTest.h"
#include "QuickStartSaveGame.h"
#include "Subsystems/QuestStateSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "SimpleQuestLog.h"
#include "BlueprintFunctionLibs/SimpleQuestBlueprintLibrary.h"
#include "Objectives/CountingQuestObjective.h"
#include "Quests/QuestStep.h"
#include "Utilities/QuestCatchUpFanout.h"
#include "Subsystems/WorldStateSubsystem.h"
#include "Quests/Types/QuestObjectiveActivationContext.h"
#include "StructUtils/InstancedStruct.h"


namespace
{
	void LogSnapshotEntries(const TCHAR* Phase, const FSimpleQuestSaveSnapshot& Snapshot)
	{
		UE_LOG(LogSimpleQuestState, Log, TEXT("[SaveLoadTest %s] %d fact(s), %d resolution key(s), %d entry key(s)"),
			Phase, Snapshot.WorldFacts.Num(), Snapshot.Resolutions.Num(), Snapshot.Entries.Num());
		for (const TPair<FGameplayTag, FQuestEntryRecord>& Pair : Snapshot.Entries)
		{
			for (const FQuestEntryArrival& Entry : Pair.Value.History)
			{
				const FQuestObjectiveAuthoredConfig& Config = Entry.ActivationContextSnapshot.Config;
				UE_LOG(LogSimpleQuestState, Log,
					TEXT("[SaveLoadTest %s]   '%s' numRequired=%d targetActors=%d targetClasses=%d instigator='%s'"),
					Phase, *Pair.Key.ToString(), Config.NumElementsRequired, Config.TargetActors.Num(),
					Config.TargetClasses.Num(), *Entry.InstigatorRef.ToString());
			}
		}
	}

	const FString GTestSlot = TEXT("SimpleQuestSaveLoadTest");

	// Run while a quest is mid-progress (its step Live). Captures state (incl. active graphs) to the disk slot.
	void SaveState(UWorld* World)
	{
		UQuickStartSaveGame* SaveObj = Cast<UQuickStartSaveGame>(UGameplayStatics::CreateSaveGameObject(UQuickStartSaveGame::StaticClass()));
		SaveObj->Snapshot = USimpleQuestBlueprintLibrary::CaptureQuestState(World);
		LogSnapshotEntries(TEXT("SAVED"), SaveObj->Snapshot);

		if (UGameplayStatics::SaveGameToSlot(SaveObj, GTestSlot, 0))
		{
			UE_LOG(LogSimpleQuestState, Log,
				TEXT("SaveState: wrote %d fact(s) / %d entry key(s) / %d graph(s) to slot '%s'. STOP + restart PIE, then run SimpleQuest.RestoreState."),
				SaveObj->Snapshot.WorldFacts.Num(), SaveObj->Snapshot.Entries.Num(), SaveObj->Snapshot.ActiveGraphs.Num(), *GTestSlot);
		}
		else
		{
			UE_LOG(LogSimpleQuestState, Warning, TEXT("SaveState: SaveGameToSlot failed."));
		}
	}

	// Run AFTER a cold PIE restart (with the questline left dormant). Restores data + every recorded graph — no path arg.
	void RestoreState(UWorld* World)
	{
		UQuickStartSaveGame* Loaded = Cast<UQuickStartSaveGame>(
			UGameplayStatics::LoadGameFromSlot(GTestSlot, 0));
		if (!Loaded)
		{
			UE_LOG(LogSimpleQuestState, Warning, TEXT("RestoreState: LoadGameFromSlot('%s') failed — run SaveState first."), *GTestSlot);
			return;
		}
		LogSnapshotEntries(TEXT("LOADED"), Loaded->Snapshot);

		USimpleQuestBlueprintLibrary::RestoreQuestState(World, Loaded->Snapshot);
		UE_LOG(LogSimpleQuestState, Log,
			TEXT("RestoreState: restored from slot '%s' (%d graph(s)). Watch LogSimpleQuestActivation for 'restored live objective'."),
			*GTestSlot, Loaded->Snapshot.ActiveGraphs.Num());
	}

	// Lists every live counting objective with its step tag + current/max. Discovery for SetCount, and the read you
	// use to verify Slice 3 (before save vs after restore).
	void ShowCounts(UWorld* World)
	{
		int32 Found = 0;
		for (TObjectIterator<UCountingQuestObjective> It; It; ++It)
		{
			UCountingQuestObjective* Obj = *It;
			if (!Obj || Obj->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject)) continue;

			// Authoritative liveness: keep only the objective its owning Step currently references. Filters out CDOs,
			// reinstanced/trashed duplicates from BP recompiles, and orphans from prior activations — all of which
			// TObjectIterator also surfaces. This is the same "live objective" set that capture walks.
			const UQuestStep* Step = Obj->GetTypedOuter<UQuestStep>();
			if (!Step || Step->GetLiveObjective() != Obj) continue;

			UE_LOG(LogSimpleQuestState, Log, TEXT("[Counts]   '%s' — %d / %d"),
				*Obj->GetOwningStepTag().ToString(), Obj->GetCurrentElements(), Obj->GetMaxElements());
			++Found;
		}
		UE_LOG(LogSimpleQuestState, Log, TEXT("[Counts] %d live counting objective(s)"), Found);
	}

	// Artificially sets a live counting objective's progress — stand-in for the gameplay/UI you don't have yet.
	// Usage: SimpleQuest.SetCount <StepTag> <N>
	void SetCount(const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() < 2)
		{
			UE_LOG(LogSimpleQuestState, Warning, TEXT("SetCount: usage 'SimpleQuest.SetCount <StepTag> <N>' — run SimpleQuest.ShowCounts for tags."));
			return;
		}
		const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(*Args[0]), false);
		if (!Tag.IsValid())
		{
			UE_LOG(LogSimpleQuestState, Warning, TEXT("SetCount: '%s' isn't a registered tag."), *Args[0]);
			return;
		}

		UCountingQuestObjective* Counting = Cast<UCountingQuestObjective>(
			USimpleQuestBlueprintLibrary::GetActiveObjectiveForTag(World, Tag));
		if (!Counting)
		{
			UE_LOG(LogSimpleQuestState, Warning, TEXT("SetCount: no live counting objective for '%s' (check SimpleQuest.ShowCounts)."), *Args[0]);
			return;
		}

		const int32 Desired = FCString::Atoi(*Args[1]);
		Counting->SetCurrentElements(Desired);
		if (Counting->GetCurrentElements() != Desired)
		{
			UE_LOG(LogSimpleQuestState, Warning, TEXT("SetCount: not applied — N must be 0..%d and differ from the current value."), Counting->GetMaxElements());
		}
		UE_LOG(LogSimpleQuestState, Log, TEXT("SetCount: '%s' -> %d / %d"),
			*Tag.ToString(), Counting->GetCurrentElements(), Counting->GetMaxElements());
	}
		UWorldStateSubsystem* GetWorldState(UWorld* World)
	{
		const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
		return GI ? GI->GetSubsystem<UWorldStateSubsystem>() : nullptr;
	}
	UQuestStateSubsystem* GetQuestState(UWorld* World)
	{
		const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
		return GI ? GI->GetSubsystem<UQuestStateSubsystem>() : nullptr;
	}

	// Activates a step with a fully-populated FQuestContextBase (instigator + a fingerprinted CustomData struct + custom /
	// origin tags), prereqs bypassed, so the entry snapshot has rich data to recover. Usage: SimpleQuest.ActivateRich <StepTag>
	void ActivateRich(const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() < 1) { UE_LOG(LogSimpleQuestState, Warning, TEXT("ActivateRich: usage 'SimpleQuest.ActivateRich <StepTag>'.")); return; }
		const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(*Args[0]), false);
		if (!Tag.IsValid()) { UE_LOG(LogSimpleQuestState, Warning, TEXT("ActivateRich: '%s' isn't a registered tag."), *Args[0]); return; }

		FSimpleQuestSaveLoadTestData TestData;
		TestData.Marker = 4242;
		TestData.Note = TEXT("payload-recovery");

		FQuestObjectiveActivationContext Params;
		Params.Instigator  = UGameplayStatics::GetPlayerPawn(World, 0);
		Params.CustomTag   = Tag;               // any valid registered tag — reused here as a simple fingerprint
		Params.OriginTag   = Tag;
		Params.OriginChain = { Tag };
		Params.CustomData  = FInstancedStruct::Make(TestData);

		USimpleQuestBlueprintLibrary::ActivateQuest(World, Tag, Params, /*bBypassPrerequisites*/ true);
		UE_LOG(LogSimpleQuestState, Log, TEXT("ActivateRich: '%s' activated (instigator=%s, CustomData{Marker=4242}). Now: DumpCatchUp -> SaveState -> cold restart -> RestoreState -> DumpCatchUp."),
			*Tag.ToString(), Params.Instigator.IsValid() ? *Params.Instigator->GetName() : TEXT("null"));
	}

	// Dumps the SHARED catch-up reconstruction (FQuestCatchUpFanout::ReconstructTag — the exact code the observer
	// component and the Observe-Quest-Lifecycle K2 node both run) for a tag. Run before save (live baseline) and after
	// RestoreState (post-load): the CustomData.Marker surviving proves the recovery + the FInstancedStruct round-trip.
	// Usage: SimpleQuest.DumpCatchUp <Tag>
	void DumpCatchUp(const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() < 1) { UE_LOG(LogSimpleQuestState, Warning, TEXT("DumpCatchUp: usage 'SimpleQuest.DumpCatchUp <Tag>'.")); return; }
		const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(*Args[0]), false);
		if (!Tag.IsValid()) { UE_LOG(LogSimpleQuestState, Warning, TEXT("DumpCatchUp: '%s' isn't a registered tag."), *Args[0]); return; }

		const FQuestCatchUpFanout::FTagReconstruction R =
			FQuestCatchUpFanout::ReconstructTag(Tag, Tag, GetWorldState(World), GetQuestState(World));

		UE_LOG(LogSimpleQuestState, Log, TEXT("DumpCatchUp '%s': channel='%s' events=%d | DisplayName='%s' Instigator=%s CustomTag='%s' OriginTag='%s' OriginChain=%d"),
			*Tag.ToString(), *R.MatchedChannel.ToString(), R.Events.Num(),
			*R.Payload.NodeInfo.DisplayName.ToString(),
			R.Payload.Instigator.IsValid() ? *R.Payload.Instigator.Get()->GetName() : TEXT("null"),
			*R.Payload.CustomTag.ToString(), *R.Payload.OriginTag.ToString(), R.Payload.OriginChain.Num());

		if (const FSimpleQuestSaveLoadTestData* Data = R.Payload.CustomData.GetPtr<FSimpleQuestSaveLoadTestData>())
		{
			UE_LOG(LogSimpleQuestState, Log, TEXT("DumpCatchUp   CustomData ROUND-TRIP OK: Marker=%d Note='%s'"), Data->Marker, *Data->Note);
		}
		else
		{
			UE_LOG(LogSimpleQuestState, Warning, TEXT("DumpCatchUp   CustomData=%s — expected FSimpleQuestSaveLoadTestData{Marker=4242}"),
				R.Payload.CustomData.IsValid() ? *R.Payload.CustomData.GetScriptStruct()->GetName() : TEXT("empty"));
		}

		for (const FQuestCatchUpFanout::FReconstructedEvent& Event : R.Events)
		{
			UE_LOG(LogSimpleQuestState, Log, TEXT("DumpCatchUp   event=%s outcome='%s' giver=%s"),
				*UEnum::GetValueAsString(Event.EventType), *Event.OutcomeTag.ToString(),
				Event.RecoveredGiver ? *Event.RecoveredGiver->GetName() : TEXT("null"));
		}
	}
}

static FAutoConsoleCommandWithWorld GSimpleQuestSaveStateCmd(
	TEXT("SimpleQuest.SaveState"),
	TEXT("Capture quest state (facts + history + active graphs) to the test save slot."),
	FConsoleCommandWithWorldDelegate::CreateStatic(&SaveState));

static FAutoConsoleCommandWithWorld GSimpleQuestRestoreStateCmd(
	TEXT("SimpleQuest.RestoreState"),
	TEXT("Load the test save and restore all recorded graphs. Run after a cold PIE restart."),
	FConsoleCommandWithWorldDelegate::CreateStatic(&RestoreState));

static FAutoConsoleCommandWithWorld GSimpleQuestShowCountsCmd(
	TEXT("SimpleQuest.ShowCounts"),
	TEXT("List every live counting objective with its step tag and current/max."),
	FConsoleCommandWithWorldDelegate::CreateStatic(&ShowCounts));

static FAutoConsoleCommandWithWorldAndArgs GSimpleQuestSetCountCmd(
	TEXT("SimpleQuest.SetCount"),
	TEXT("Set a live counting objective's progress: SimpleQuest.SetCount <StepTag> <N>."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SetCount));

static FAutoConsoleCommandWithWorldAndArgs GSimpleQuestActivateRichCmd(
	TEXT("SimpleQuest.ActivateRich"),
	TEXT("Activate a step with a populated context (instigator + fingerprinted CustomData): SimpleQuest.ActivateRich <StepTag>."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ActivateRich));

static FAutoConsoleCommandWithWorldAndArgs GSimpleQuestDumpCatchUpCmd(
	TEXT("SimpleQuest.DumpCatchUp"),
	TEXT("Dump the shared catch-up reconstruction (ReconstructTag) payload for a tag: SimpleQuest.DumpCatchUp <Tag>."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&DumpCatchUp));

