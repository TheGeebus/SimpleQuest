// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "SimpleCoreEditor.h"

#include "SimpleCoreEditorLog.h"
#include "Debug/SimpleCorePIEDebugChannel.h"
#include "FactsPanel/FactsPanelRegistry.h"
#include "Widgets/SFactsPanel.h"
#include "Widgets/SWorldStateFactsView.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "Modules/ModuleManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#define LOCTEXT_NAMESPACE "FSimpleCoreEditorModule"

DEFINE_LOG_CATEGORY(LogSimpleCoreEditor);

namespace
{
    /** Set true when FCoreDelegates::OnExit fires. Read by SpawnFactsPanelTab's OnTabClosed handler to skip
     *  the persisted-state cleanup during editor shutdown — docked tabs in the saved layout need their state
     *  preserved for next-session restore; only user-initiated closes should wipe state. File-scope static so
     *  the OnTabClosed lambda doesn't need to capture `this` (which would dangle if module unloads before the
     *  tab destructs). */
    bool GFactsPanelEditorShuttingDown = false;
}

const FName FSimpleCoreEditorModule::FactsPanelTabId(TEXT("SimpleCore.FactsPanel"));
const FName FSimpleCoreEditorModule::WorldStateViewId(TEXT("SimpleCore.WorldState"));

void FSimpleCoreEditorModule::StartupModule()
{
    PIEDebugChannel = MakeUnique<FSimpleCorePIEDebugChannel>();
    PIEDebugChannel->Initialize();

    OnExitHandle = FCoreDelegates::OnExit.AddLambda([]()
    {
        GFactsPanelEditorShuttingDown = true;
    });

    // Register the WorldState facts view with the generic facts-panel registry. Each SFactsPanel instance gets its own
    // SWorldStateFactsView via this factory, so per-panel filter / scroll state stays isolated across docked panels.
    FFactsPanelRegistry::Get().RegisterView(
        WorldStateViewId,
        LOCTEXT("WorldStateViewName", "World State"),
        [](FName PanelPersistenceKey) -> TSharedRef<SWidget> { return SNew(SWorldStateFactsView); });

    // Nomad tab under Window > Developer Tools. SetReuseTabMethod returning null forces a fresh SFactsPanel on each
    // menu invocation - designers can dock multiple panels showing different registries side-by-side.
    FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
        FactsPanelTabId,
        FOnSpawnTab::CreateRaw(this, &FSimpleCoreEditorModule::SpawnFactsPanelTab))
        .SetDisplayName(LOCTEXT("FactsPanelTabTitle", "Facts Panel"))
        .SetTooltipText(LOCTEXT("FactsPanelTabTooltip", "Live view of registered fact registries (WorldState, Quest State, etc.). Re-invoke for additional panels."))
        .SetGroup(WorkspaceMenu::GetMenuStructure().GetDeveloperToolsDebugCategory())
        .SetMenuType(ETabSpawnerMenuType::Enabled)
        .SetReuseTabMethod(FOnFindTabToReuse::CreateLambda([](const FTabId&) -> TSharedPtr<SDockTab> { return nullptr; }));

    UE_LOG(LogSimpleCoreEditor, Display, TEXT("FSimpleCoreEditorModule::StartupModule — PIE debug channel initialized, WorldState facts view registered, Facts Panel tab registered"));
}

void FSimpleCoreEditorModule::ShutdownModule()
{
    if (OnExitHandle.IsValid())
    {
        FCoreDelegates::OnExit.Remove(OnExitHandle);
        OnExitHandle.Reset();
    }
    
    if (FSlateApplication::IsInitialized())
    {
        FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(FactsPanelTabId);
    }

    FFactsPanelRegistry::Get().UnregisterView(WorldStateViewId);

    if (PIEDebugChannel.IsValid())
    {
        PIEDebugChannel->Shutdown();
        PIEDebugChannel.Reset();
    }

    UE_LOG(LogSimpleCoreEditor, Display, TEXT("FSimpleCoreEditorModule::ShutdownModule — Facts Panel tab unregistered, WorldState view deregistered, PIE debug channel torn down"));
}

FSimpleCorePIEDebugChannel* FSimpleCoreEditorModule::GetPIEDebugChannel()
{
    if (FSimpleCoreEditorModule* Module = FModuleManager::GetModulePtr<FSimpleCoreEditorModule>("SimpleCoreEditor"))
    {
        return Module->PIEDebugChannel.Get();
    }
    return nullptr;
}

TSharedRef<SDockTab> FSimpleCoreEditorModule::SpawnFactsPanelTab(const FSpawnTabArgs& Args)
{
    // Per-panel persistence key derived from spawn order within the editor session. Layout-restored tabs spawn
    // in the same order each session (depth-first walk of the saved dock tree), so Panel_N consistently maps
    // to the same docked panel across editor restarts. Menu-spawned ephemeral tabs pick up wherever the docked
    // count leaves off — no saved entry for that key on first spawn, so they fall through to FallbackViewID
    // (WorldState).
    //
    // Counter-based rather than InstanceId-based because FTabId.InstanceId for nomad tabs isn't reliably
    // preserved across sessions in UE 5.6's layout system. The counter resets per session (module member,
    // re-zeroed on StartupModule), but layout-restore order stability gives us de-facto per-panel identity.
    ++SpawnedPanelCount;
    const FName PersistenceKey = FName(*FString::Printf(TEXT("Panel_%d"), SpawnedPanelCount));

    UE_LOG(LogSimpleCoreEditor, Log,
        TEXT("FSimpleCoreEditorModule::SpawnFactsPanelTab : SpawnedPanelCount=%d, PersistenceKey='%s'"),
        SpawnedPanelCount, *PersistenceKey.ToString());

    TSharedRef<SFactsPanel> Panel = SNew(SFactsPanel)
        .PersistenceKey(PersistenceKey)
        .FallbackViewID(WorldStateViewId);

    return SNew(SDockTab)
        .TabRole(ETabRole::NomadTab)
        .Label_Lambda([Panel]() { return Panel->GetActiveViewLabel(); })
        .OnTabClosed_Lambda([PersistenceKey](TSharedRef<SDockTab>)
        {
            // Clear persisted state on user-initiated close so the next menu-spawn at this counter slot
            // starts at FallbackViewID (WorldState) rather than inheriting whatever the previous occupant
            // left behind. Skip during editor shutdown — docked layout tabs need their state preserved for
            // next-session restore.
            if (GFactsPanelEditorShuttingDown) return;

            const FString LastViewKey = FString::Printf(TEXT("%s.LastView"), *PersistenceKey.ToString());
            const FString TabKey      = FString::Printf(TEXT("%s.QuestStateActiveTab"), *PersistenceKey.ToString());
            GConfig->RemoveKey(TEXT("FactsPanel"), *LastViewKey, GEditorPerProjectIni);
            GConfig->RemoveKey(TEXT("FactsPanel"), *TabKey,      GEditorPerProjectIni);
            GConfig->Flush(false, GEditorPerProjectIni);

            UE_LOG(LogSimpleCoreEditor, Log,
                TEXT("FSimpleCoreEditorModule::OnTabClosed : cleared persisted state for '%s'"),
                *PersistenceKey.ToString());
        })
        [
            Panel
        ];
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FSimpleCoreEditorModule, SimpleCoreEditor)