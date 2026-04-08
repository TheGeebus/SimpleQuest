// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

class FQuestlineGraphCompiler;

// Factory delegate type. Must return a valid non-null compiler instance.
DECLARE_DELEGATE_RetVal(TUniquePtr<FQuestlineGraphCompiler>, FQuestlineCompilerFactoryDelegate);

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
     * Register native Gameplay Tags produced at graph compilation time. For each quest tag registered, the corresponding WorldState
     * fact tags (Active, Succeeded, Failed, PendingGiver) are automatically registered in the same pass. Any caller — compiler,
     * importer, or procedural generator — receives this guarantee by calling through this interface.
     */
    virtual void RegisterCompiledTags(const FString& GraphPath, const TArray<FName>& TagNames) = 0;

};
