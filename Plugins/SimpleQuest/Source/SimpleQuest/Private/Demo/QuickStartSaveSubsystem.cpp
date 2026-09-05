// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "QuickStartSaveSubsystem.h"

#include "Engine/World.h"
#include "TimerManager.h"
#include "Kismet/GameplayStatics.h"
#include "QuickStartSaveGame.h"
#include "SimpleQuestLog.h"
#include "BlueprintFunctionLibs/SimpleQuestBlueprintLibrary.h"

void UQuickStartSaveSubsystem::ForEachLiveTarget(TFunctionRef<void(UObject&)> Visit)
{
	for (int32 Index = SaveTargets.Num() - 1; Index >= 0; --Index)
	{
		if (UObject* Target = SaveTargets[Index].Get())
		{
			Visit(*Target);
		}
		else
		{
			SaveTargets.RemoveAtSwap(Index);   // destroyed between registration and now; nothing to persist
		}
	}
}

void UQuickStartSaveSubsystem::RequestSave(const FString& SlotName)
{
	UQuickStartSaveGame* Save = Cast<UQuickStartSaveGame>(
		UGameplayStatics::CreateSaveGameObject(UQuickStartSaveGame::StaticClass()));
	if (!Save)
	{
		UE_LOG(LogSimpleQuestActivation, Warning, TEXT("RequestSave: could not create a save object for slot '%s'."), *SlotName);
		return;
	}

	// CaptureQuestState rather than UQuestStateSubsystem::CaptureSnapshot: the library pairs the snapshot with the
	// active-graph list and deferred-activation set that a restore needs, which the lower-level primitive does not.
	Save->Snapshot  = USimpleQuestBlueprintLibrary::CaptureQuestState(GetGameInstance());
	Save->LevelName = FName(*UGameplayStatics::GetCurrentLevelName(GetGameInstance(), true));

	// Every target writes its OWN fields. Nothing here reaches into an actor to read them, which is why this function
	// does not know what a pawn is and does not break when a second kind of thing needs persisting.
	ForEachLiveTarget([Save](UObject& Target)
	{
		IQuickStartSaveTarget::Execute_CaptureSaveData(&Target, Save);
	});

	UGameplayStatics::SaveGameToSlot(Save, SlotName, 0);
	UE_LOG(LogSimpleQuestActivation, Log, TEXT("RequestSave: wrote slot '%s' from level '%s'."), *SlotName, *Save->LevelName.ToString());
}

void UQuickStartSaveSubsystem::RequestLoad(const FString& SlotName)
{
	UQuickStartSaveGame* Save = Cast<UQuickStartSaveGame>(UGameplayStatics::LoadGameFromSlot(SlotName, 0));
	if (!Save)
	{
		UE_LOG(LogSimpleQuestActivation, Warning, TEXT("RequestLoad: no save in slot '%s'."), *SlotName);
		return;
	}

	PendingRestore = Save;
	ArmRestoreConsumption();

	// *** ORDER IS IMPORTANT. *** ApplyQuestSnapshot with bRestoreOnNextLevelLoad has to run BEFORE the level opens:
	// it applies the data now and leaves the per-graph rebuild staged for the reload, so the world reconstructs itself
	// and catch-up fires as components subscribe at spawn. Opening first would restore into a world about to be thrown
	// away.
	USimpleQuestBlueprintLibrary::ApplyQuestSnapshot(GetGameInstance(), Save->Snapshot, true);

	if (Save->LevelName.IsNone())
	{
		UE_LOG(LogSimpleQuestActivation, Warning,
			TEXT("RequestLoad: slot '%s' records no level - written before saves carried one. Reopening the current level."), *SlotName);
		UGameplayStatics::OpenLevel(GetGameInstance(), FName(*UGameplayStatics::GetCurrentLevelName(GetGameInstance(), true)));
		return;
	}

	UGameplayStatics::OpenLevel(GetGameInstance(), Save->LevelName);
}

bool UQuickStartSaveSubsystem::HasSave(const FString& SlotName) const
{
	return UGameplayStatics::DoesSaveGameExist(SlotName, 0);
}

bool UQuickStartSaveSubsystem::RegisterSaveTarget(const TScriptInterface<IQuickStartSaveTarget>& Target)
{
	UObject* Object = Target.GetObject();
	if (!Object)
	{
		UE_LOG(LogSimpleQuestActivation, Warning,
			TEXT("RegisterSaveTarget: called with an unset target. Nothing was registered, so this object will "
				 "neither contribute to a save nor receive a restore."));
		return false;
	}

	SaveTargets.AddUnique(Object);

	// CATCH-UP, the same shape a quest observer uses: something arriving after the state was staged still receives it.
	// That lets a character call this from BeginPlay without knowing whether it spawned into a fresh game or a restored one.
	if (PendingRestore)
	{
		IQuickStartSaveTarget::Execute_ApplySaveData(Object, PendingRestore);
		return true;
	}
	return false;
}

void UQuickStartSaveSubsystem::ArmRestoreConsumption()
{
	if (PostWorldInitHandle.IsValid()) return;   // idempotent, same as the manager's arm
	PostWorldInitHandle = FWorldDelegates::OnPostWorldInitialization.AddUObject(this, &UQuickStartSaveSubsystem::HandleWorldInitForRestore);
}

void UQuickStartSaveSubsystem::HandleWorldInitForRestore(UWorld* World, const UWorld::InitializationValues)
{
	if (!World || !World->IsGameWorld()) return;

	FWorldDelegates::OnPostWorldInitialization.Remove(PostWorldInitHandle);
	PostWorldInitHandle.Reset();

	// *** THE RESTORE IS A WAVE, NOT A STANDING STATE. *** Targets register from BeginPlay - after world init,
	// before the first tick - so hold the staged save across that window and consume it on the next tick. Anything
	// registering later spawned into an ongoing session and initializes itself rather than being handed save-time
	// state that has since gone stale.
	TWeakObjectPtr<UQuickStartSaveSubsystem> WeakThis(this);
	World->GetTimerManager().SetTimerForNextTick([WeakThis]()
	{
		if (UQuickStartSaveSubsystem* Self = WeakThis.Get())
		{
			UE_LOG(LogSimpleQuestActivation, Log,
				TEXT("Restore wave complete - %d target(s) registered. Clearing the staged save."), Self->SaveTargets.Num());
			Self->PendingRestore = nullptr;
		}
	});
}

void UQuickStartSaveSubsystem::Deinitialize()
{
	if (PostWorldInitHandle.IsValid())
	{
		FWorldDelegates::OnPostWorldInitialization.Remove(PostWorldInitHandle);
		PostWorldInitHandle.Reset();
	}
	Super::Deinitialize();
}

