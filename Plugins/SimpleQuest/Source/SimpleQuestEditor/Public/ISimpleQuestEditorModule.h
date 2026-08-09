// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

class UQuestlineGraph;
class FQuestlineGraphCompiler;

// Factory delegate type. Must return a valid non-null compiler instance.
DECLARE_DELEGATE_RetVal(TUniquePtr<FQuestlineGraphCompiler>, FQuestlineCompilerFactoryDelegate);

DECLARE_MULTICAST_DELEGATE_TwoParams(FOnQuestlineCompiled, const FString& /*PackagePath*/, bool /*bSuccess*/);

/**
 * Public interface for the SimpleQuestEditor module.
 *
 * External modules should access this interface via ISimpleQuestEditorModule::Get() rather than depending on the concrete
 * FSimpleQuestEditor class.
 *
 * To provide a custom questline graph compiler, register a factory delegate during your module's StartupModule and unregister
 * it in ShutdownModule:
 *
 *   ISimpleQuestEditorModule::Get().RegisterCompilerFactory(
 *       FQuestlineCompilerFactoryDelegate::CreateLambda([]()
 *       {
 *           return MakeUnique<FMyCustomCompiler>();
 *       }));
 */
class SIMPLEQUESTEDITOR_API ISimpleQuestEditorModule : public IModuleInterface
{
public:
    static ISimpleQuestEditorModule& Get()
    {
        return FModuleManager::GetModuleChecked<ISimpleQuestEditorModule>("SimpleQuestEditor");
    }

    static bool IsAvailable()
    {
        return FModuleManager::Get().IsModuleLoaded("SimpleQuestEditor");
    }

    /**
     * Register a factory that produces custom FQuestlineGraphCompiler instances. Only one factory may be registered at
     * a time. Registering a second factory will replace the first. Unregister in your module's ShutdownModule.
     */
    virtual void RegisterCompilerFactory(FQuestlineCompilerFactoryDelegate InFactory) = 0;

    /**
     * Remove the currently registered compiler factory, restoring the default compiler.
     */
    virtual void UnregisterCompilerFactory() = 0;

    /**
     * Creates a compiler instance using the registered factory if one exists, otherwise returns the default FQuestlineGraphCompiler.
     */
    virtual TUniquePtr<FQuestlineGraphCompiler> CreateCompiler() const = 0;
    
    /**
     * Begin / End a compile batch. Inside a batch, RegisterCompiledTags defers WriteCompiledTagsIni and the native-tag tree
     * rebuild to EndCompileBatch — incremental AddNativeTagsForGraph calls keep mid-batch RequestGameplayTag lookups working,
     * then a single coalesced rebuild + refresh fires when the batch closes. Callers that need a redirect-map update to land
     * between the per-graph compiles and the final native-tag rebuild (so rebuilt tags register under the new redirect map)
     * should call WriteGameplayTagRedirects inside the batch scope, just before it closes.
     *
     * BeginCompileBatch must be paired with EndCompileBatch. Use ON_SCOPE_EXIT for cancel safety.
     */
    virtual void BeginCompileBatch() = 0;
    virtual void EndCompileBatch() = 0;

    /**
     * Register native Gameplay Tags produced at graph compilation time. For each quest tag registered, the corresponding WorldState
     * fact tags (Active, Succeeded, Failed, PendingGiver) are automatically registered in the same pass. Any caller — compiler,
     * importer, or procedural generator — receives this guarantee by calling through this interface.
     */
    virtual void RegisterCompiledTags(const FString& GraphPath, const TArray<FName>& TagNames) = 0;

    /**
     * Collects every UQuestlineGraph asset connected to Primary via LinkedQuestline references — both forward
     * (assets Primary references) and backward (assets that reference Primary), transitively closed with a
     * visited set to handle cycles. Walks the Asset Registry's package dependency graph (EDependencyCategory::Package,
     * covers hard + soft refs; LinkedGraph is TSoftObjectPtr). Sync-loads each neighbor on first discovery.
     * Output excludes Primary itself — caller compiles Primary separately.
     *
     * Used by single-asset Compile to auto-compile the linked neighborhood so contextual tags across assets
     * stay in sync without requiring Compile All.
     */
    virtual void CollectLinkedNeighborhood(UQuestlineGraph* Primary, TArray<UQuestlineGraph*>& OutNeighborhood) const = 0;

    /**
     * Compile Primary and its linked neighborhood as one batch, coalescing the tag-tree rebuild and the redirect write
     * the way the editor's Compile action does. Returns whether every graph compiled; OutCompiledCount reports how many
     * were touched, Primary included.
     *
     * Exists so a caller that MUTATES a questline can leave it consistent without owning the compiler's sequencing. A
     * questline that LINKS this one embeds its compiled data (node aliases, entry-step tags), so compiling the target
     * alone would fix the asset and leave every linked parent describing a graph that no longer exists.
     *
     * Reports nothing to the user - no message log, no notification. The caller decides how to say it, which is what
     * lets the console command, the editor toolbar and a headless commandlet share one definition.
     */
    virtual bool CompileQuestlineAndNeighborhood(UQuestlineGraph* Primary, int32& OutCompiledCount) = 0;
    
    virtual void CompileAllQuestlineGraphs() = 0;
    
    /**
     * Record this graph's freshly compiled display data for the deferred display-ini write. Cheap + in-memory — the
     * file write is coalesced to once per batch in EndCompileBatch, the same way RegisterCompiledTags defers WriteCompiledTagsIni.
     */
    virtual void AccumulateCompiledDisplay(const UQuestlineGraph* Graph) = 0;

    virtual FOnQuestlineCompiled& OnQuestlineCompiled() = 0;
};
