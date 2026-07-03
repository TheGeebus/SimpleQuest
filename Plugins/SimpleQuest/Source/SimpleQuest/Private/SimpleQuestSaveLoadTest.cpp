// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

// THROWAWAY verification harness for save/load Slice 1. Console command "SimpleQuest.SaveLoadRoundTrip" captures quest
// state, round-trips it through a save slot, applies it, and logs CAPTURED vs LOADED so the SaveGame field flags can be
// eyeballed. Delete this file and its header once Slice 1 is verified.

#include "SimpleQuestSaveLoadTest.h"
#include "Subsystems/QuestStateSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "SimpleQuestLog.h"
#include "BlueprintFunctionLibs/SimpleQuestBlueprintLibrary.h"

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
		USimpleQuestSaveLoadTestSave* SaveObj = Cast<USimpleQuestSaveLoadTestSave>(
			UGameplayStatics::CreateSaveGameObject(USimpleQuestSaveLoadTestSave::StaticClass()));
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
		USimpleQuestSaveLoadTestSave* Loaded = Cast<USimpleQuestSaveLoadTestSave>(
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
}

static FAutoConsoleCommandWithWorld GSimpleQuestSaveStateCmd(
	TEXT("SimpleQuest.SaveState"),
	TEXT("Capture quest state (facts + history + active graphs) to the test save slot."),
	FConsoleCommandWithWorldDelegate::CreateStatic(&SaveState));

static FAutoConsoleCommandWithWorld GSimpleQuestRestoreStateCmd(
	TEXT("SimpleQuest.RestoreState"),
	TEXT("Load the test save and restore all recorded graphs. Run after a cold PIE restart."),
	FConsoleCommandWithWorldDelegate::CreateStatic(&RestoreState));