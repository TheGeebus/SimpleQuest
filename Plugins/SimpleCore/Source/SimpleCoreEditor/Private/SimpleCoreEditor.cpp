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

const FName FSimpleCoreEditorModule::FactsPanelTabId(TEXT("SimpleCore.FactsPanel"));
const FName FSimpleCoreEditorModule::WorldStateViewId(TEXT("SimpleCore.WorldState"));

void FSimpleCoreEditorModule::StartupModule()
{
    PIEDebugChannel = MakeUnique<FSimpleCorePIEDebugChannel>();
    PIEDebugChannel->Initialize();

    // Register the WorldState facts view with the generic facts-panel registry. Each SFactsPanel instance gets its own
    // SWorldStateFactsView via this factory, so per-panel filter / scroll state stays isolated across docked panels.
    FFactsPanelRegistry::Get().RegisterView(
        WorldStateViewId,
        LOCTEXT("WorldStateViewName", "World State Facts"),
        []() -> TSharedRef<SWidget> { return SNew(SWorldStateFactsView); });

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
    ++SpawnedPanelCount;
    const FText TabLabel = SpawnedPanelCount == 1
        ? LOCTEXT("FactsPanelTabLabel", "Facts Panel")
        : FText::Format(LOCTEXT("FactsPanelTabLabelN", "Facts Panel {0}"), FText::AsNumber(SpawnedPanelCount));

    return SNew(SDockTab)
        .TabRole(ETabRole::NomadTab)
        .Label(TabLabel)
        [
            SNew(SFactsPanel)
        ];
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FSimpleCoreEditorModule, SimpleCoreEditor)