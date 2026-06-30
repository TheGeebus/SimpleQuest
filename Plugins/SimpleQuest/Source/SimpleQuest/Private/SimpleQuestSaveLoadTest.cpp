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

	void SaveLoadRoundTrip(UWorld* World)
	{
		UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
		UQuestStateSubsystem* QSS = GI ? GI->GetSubsystem<UQuestStateSubsystem>() : nullptr;
		if (!QSS)
		{
			UE_LOG(LogSimpleQuestState, Warning, TEXT("SaveLoadRoundTrip: no QuestStateSubsystem (run this in PIE)."));
			return;
		}

		USimpleQuestSaveLoadTestSave* SaveObj = Cast<USimpleQuestSaveLoadTestSave>(
			UGameplayStatics::CreateSaveGameObject(USimpleQuestSaveLoadTestSave::StaticClass()));
		SaveObj->Snapshot = QSS->CaptureSnapshot();
		LogSnapshotEntries(TEXT("CAPTURED"), SaveObj->Snapshot);

		const FString Slot = TEXT("SimpleQuestSaveLoadTest");
		if (!UGameplayStatics::SaveGameToSlot(SaveObj, Slot, 0))
		{
			UE_LOG(LogSimpleQuestState, Warning, TEXT("SaveLoadRoundTrip: SaveGameToSlot failed."));
			return;
		}

		USimpleQuestSaveLoadTestSave* Loaded = Cast<USimpleQuestSaveLoadTestSave>(
			UGameplayStatics::LoadGameFromSlot(Slot, 0));
		if (!Loaded)
		{
			UE_LOG(LogSimpleQuestState, Warning, TEXT("SaveLoadRoundTrip: LoadGameFromSlot failed."));
			return;
		}
		LogSnapshotEntries(TEXT("LOADED"), Loaded->Snapshot);

		QSS->ApplySnapshot(Loaded->Snapshot);
		UE_LOG(LogSimpleQuestState, Log,
			TEXT("SaveLoadRoundTrip: applied loaded snapshot. Compare CAPTURED vs LOADED above — Config fields and the "
			     "instigator path should match; a mismatch means a SaveGame flag isn't taking."));
	}
}

static FAutoConsoleCommandWithWorld GSimpleQuestSaveLoadRoundTripCmd(
	TEXT("SimpleQuest.SaveLoadRoundTrip"),
	TEXT("Capture quest state, round-trip it through a save slot, apply it, and log CAPTURED vs LOADED (Slice 1 verification)."),
	FConsoleCommandWithWorldDelegate::CreateStatic(&SaveLoadRoundTrip));