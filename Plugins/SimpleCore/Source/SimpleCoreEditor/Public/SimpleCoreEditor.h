// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

class FSimpleCorePIEDebugChannel;
class FSpawnTabArgs;
class SDockTab;

/**
 * Editor module for the SimpleCore plugin. Owns the editor-side PIE debug channel scoped to SimpleCore's subsystems
 * (WorldState today; additional subsystem hooks may slot in later), and registers the WorldState Facts inspector as
 * a nomad tab accessible from the editor's Window menu under Developer Tools.
 *
 * Independent of SimpleQuest — a project using SimpleCore without SimpleQuest still gets the WorldState inspector.
 */
class FSimpleCoreEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** Access the editor module's PIE debug channel. Returns nullptr if the module isn't loaded. */
	static FSimpleCorePIEDebugChannel* GetPIEDebugChannel();

private:
	TSharedRef<SDockTab> SpawnWorldStateFactsTab(const FSpawnTabArgs& Args);

	TUniquePtr<FSimpleCorePIEDebugChannel> PIEDebugChannel;

	static const FName WorldStateFactsTabId;
};
