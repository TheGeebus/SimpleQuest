// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

class FSimpleCorePIEDebugChannel;
class FSpawnTabArgs;
class SDockTab;

/**
 * Editor module for the SimpleCore plugin. Owns the editor-side PIE debug channel scoped to SimpleCore's subsystems
 * (WorldState today; additional subsystem hooks may slot in later), registers the WorldState facts view with the
 * generic facts-panel registry, and registers a multi-instance Facts Panel as a nomad tab under Window → Developer
 * Tools.
 *
 * Independent of SimpleQuest — a project using SimpleCore without SimpleQuest still gets the WorldState view and the
 * panel shell.
 */
class SIMPLECOREEDITOR_API FSimpleCoreEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** Access the editor module's PIE debug channel. Returns nullptr if the module isn't loaded. */
	static FSimpleCorePIEDebugChannel* GetPIEDebugChannel();

private:
	TSharedRef<SDockTab> SpawnFactsPanelTab(const FSpawnTabArgs& Args);

	TUniquePtr<FSimpleCorePIEDebugChannel> PIEDebugChannel;

	/** Counter appended to spawned tab labels for multi-instance disambiguation. */
	int32 SpawnedPanelCount = 0;

	static const FName FactsPanelTabId;
	static const FName WorldStateViewId;
};