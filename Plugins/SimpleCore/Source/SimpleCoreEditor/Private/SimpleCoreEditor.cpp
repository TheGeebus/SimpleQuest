// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "SimpleCoreEditor.h"
#include "Debug/SimpleCorePIEDebugChannel.h"
#include "Widgets/SWorldStateFactsPanel.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "Modules/ModuleManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#define LOCTEXT_NAMESPACE "FSimpleCoreEditorModule"

DEFINE_LOG_CATEGORY_STATIC(LogSimpleCoreEditor, Log, All);

const FName FSimpleCoreEditorModule::WorldStateFactsTabId(TEXT("SimpleCore.WorldStateFacts"));

void FSimpleCoreEditorModule::StartupModule()
{
    // Lifecycle: construct + Initialize here, Shutdown + reset in ShutdownModule. Matches FQuestPIEDebugChannel's pattern.
    // Zero runtime cost outside PIE — the channel just holds a pair of editor-delegate handles.
    PIEDebugChannel = MakeUnique<FSimpleCorePIEDebugChannel>();
    PIEDebugChannel->Initialize();

    // Nomad tab under Window → Developer Tools. RegisterNomadTabSpawner returns a FTabSpawnerEntry we can fluently
    // configure; SetGroup() pins it under the editor's standard developer-tools submenu so it discoverability matches
    // UE's built-in debug inspectors (Output Log, Session Frontend, etc.).
    FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
        WorldStateFactsTabId,
        FOnSpawnTab::CreateRaw(this, &FSimpleCoreEditorModule::SpawnWorldStateFactsTab))
        .SetDisplayName(LOCTEXT("WorldStateFactsTabTitle", "World State Facts"))
        .SetTooltipText(LOCTEXT("WorldStateFactsTabTooltip", "Live view of all facts asserted in the PIE world's UWorldStateSubsystem."))
        .SetGroup(WorkspaceMenu::GetMenuStructure().GetDeveloperToolsDebugCategory())
        .SetMenuType(ETabSpawnerMenuType::Enabled);

    UE_LOG(LogSimpleCoreEditor, Display, TEXT("FSimpleCoreEditorModule::StartupModule — PIE debug channel initialized, WorldState Facts tab registered"));
}

void FSimpleCoreEditorModule::ShutdownModule()
{
    if (FSlateApplication::IsInitialized())
    {
        FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(WorldStateFactsTabId);
    }

    if (PIEDebugChannel.IsValid())
    {
        PIEDebugChannel->Shutdown();
        PIEDebugChannel.Reset();
    }

    UE_LOG(LogSimpleCoreEditor, Display, TEXT("FSimpleCoreEditorModule::ShutdownModule — tab unregistered, PIE debug channel torn down"));
}

FSimpleCorePIEDebugChannel* FSimpleCoreEditorModule::GetPIEDebugChannel()
{
    if (FSimpleCoreEditorModule* Module = FModuleManager::GetModulePtr<FSimpleCoreEditorModule>("SimpleCoreEditor"))
    {
        return Module->PIEDebugChannel.Get();
    }
    return nullptr;
}

TSharedRef<SDockTab> FSimpleCoreEditorModule::SpawnWorldStateFactsTab(const FSpawnTabArgs& Args)
{
    // NomadTab is the standalone-window variant. The panel itself is self-contained — owns its own Tick for live
    // refresh while the channel is active.
    return SNew(SDockTab)
        .TabRole(ETabRole::NomadTab)
        [
            SNew(SWorldStateFactsPanel)
        ];
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FSimpleCoreEditorModule, SimpleCoreEditor)

