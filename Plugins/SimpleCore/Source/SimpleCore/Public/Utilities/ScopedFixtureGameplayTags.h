// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "GameplayTagsManager.h"
#include "NativeGameplayTags.h"
#include "Utilities/SimpleCoreLog.h"

/**
 * Registers an automation test's fixture tags for as long as this object lives, and unregisters them when it goes.
 *
 * WHY NOT UE_DEFINE_GAMEPLAY_TAG_STATIC. A static native tag registers when its module loads, and every editor build compiles
 * the tests in - so a fixture declared that way sits in the tag pickers of every project that installs the plugin, for the
 * whole session, whether or not a test ever runs. Scoped to the test, it exists only while the test does.
 *
 * Each binding names a variable to fill. While this object lives, the variable holds the registered tag; when it goes, the
 * variable is reset to an empty tag. A test that forgets to open a scope therefore fails on an invalid tag rather than
 * passing on one an earlier test left behind.
 *
 * The caller passes UE_PLUGIN_NAME and UE_MODULE_NAME - the same two names the static macro records - and they must name a
 * runtime module, since a native tag declared from an editor-only module trips an engine ensure. They are parameters rather
 * than written in here because this header is shared by more than one module, and an inline function that expanded them
 * itself would differ from module to module.
 *
 * If the project forbids unloading tags in the editor (Gameplay Tags settings, Allow Editor Tag Unloading), the engine
 * ignores the unregistration and the tags stay until the tag tree is next rebuilt. That only happens in a session where
 * tests ran, and it is logged.
 *
 * Header-only and gated by WITH_DEV_AUTOMATION_TESTS, so none of it exists in a shipping build.
 */
class FScopedFixtureGameplayTags
{
public:
	/** One fixture tag: the variable the test reads it through, and the name to register. */
	struct FBinding
	{
		FGameplayTag& Target;
		const TCHAR* TagName;
	};

	FScopedFixtureGameplayTags(FName PluginName, FName ModuleName, std::initializer_list<FBinding> Bindings)
	{
		for (const FBinding& Binding : Bindings)
		{
			const TUniquePtr<FNativeGameplayTag>& Native = Natives.Add_GetRef(MakeUnique<FNativeGameplayTag>(PluginName, ModuleName,
				FName(Binding.TagName), TEXT("Automation test fixture, registered only while its test runs"),
				ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD));
			Binding.Target = Native->GetTag();
			Targets.Add(&Binding.Target);
		}

		UE_LOG(LogSimpleCore, Verbose, TEXT("FScopedFixtureGameplayTags: registered %d fixture tag(s) for %s, first '%s'"),
			Natives.Num(),
			*ModuleName.ToString(),
			Natives.Num() > 0 ? *Natives[0]->GetTag().ToString() : TEXT("(none)"));
	}

	~FScopedFixtureGameplayTags()
	{
		for (FGameplayTag* Target : Targets)
		{
			*Target = FGameplayTag();
		}

		UGameplayTagsManager& Manager = UGameplayTagsManager::Get();
		const int32 Count = Natives.Num();

#if WITH_EDITOR
		// Each native's destructor asks the editor for a full tag-tree rebuild - one per tag. Suspended, the whole set costs a
		// single rebuild, run when the suspension lifts.
		const FGuid SuspendToken = FGuid::NewGuid();
		Manager.SuspendEditorRefreshGameplayTagTree(SuspendToken);
#endif
		Natives.Reset();
#if WITH_EDITOR
		Manager.ResumeEditorRefreshGameplayTagTree(SuspendToken);
#endif

		if (Manager.ShouldUnloadTags())
		{
			UE_LOG(LogSimpleCore, Verbose, TEXT("FScopedFixtureGameplayTags: unregistered %d fixture tag(s)"), Count);
		}
		else
		{
			UE_LOG(LogSimpleCore, Log, TEXT("FScopedFixtureGameplayTags: this project does not allow unloading gameplay tags, so %d "
				"fixture tag(s) stay in the tag tree until it is next rebuilt"), Count);
		}
	}

	UE_NONCOPYABLE(FScopedFixtureGameplayTags)

private:
	TArray<TUniquePtr<FNativeGameplayTag>> Natives;
	TArray<FGameplayTag*> Targets;
};

#endif // WITH_DEV_AUTOMATION_TESTS

