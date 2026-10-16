// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#if WITH_DEV_AUTOMATION_TESTS

#include "GameplayTagsManager.h"
#include "Misc/AutomationTest.h"
#include "Utilities/ScopedFixtureGameplayTags.h"

namespace
{
	constexpr EAutomationTestFlags FixtureTagTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	/** Names nothing else registers, so "absent" can only mean this helper cleaned up after itself. */
	const TCHAR* const ScopeFixtureRootName  = TEXT("SimpleCore.ScopeFixture");
	const TCHAR* const ScopeFixtureChildName = TEXT("SimpleCore.ScopeFixture.Child");

	FGameplayTag TAG_ScopeFixture_Root;
	FGameplayTag TAG_ScopeFixture_Child;

	bool IsRegistered(const TCHAR* TagName)
	{
		return UGameplayTagsManager::Get().RequestGameplayTag(FName(TagName), /*ErrorIfNotFound*/ false).IsValid();
	}
}

// -------------------------------------------------------------------------------------------------
// The property every other test's fixture tags now rest on: a fixture tag is in the tree while its
// scope is open and nowhere else. Registered at module load instead, a fixture sits in every
// project's tag pickers for the whole session - which is what this helper replaced.
// -------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFixtureTags_LiveOnlyInsideTheScope,
	"SimpleCore.TestSupport.FixtureTags.LiveOnlyInsideTheScope", FixtureTagTestFlags)
bool FFixtureTags_LiveOnlyInsideTheScope::RunTest(const FString& Parameters)
{
	// A project can forbid unloading tags in the editor, and then the tags stay until the next tree rebuild. "Gone
	// afterwards" cannot be asked there, and neither can "absent before" on any run after the first.
	const bool bCanUnload = UGameplayTagsManager::Get().ShouldUnloadTags();
	if (bCanUnload)
	{
		TestFalse(TEXT("not registered before the scope opens"), IsRegistered(ScopeFixtureChildName));
	}
	else
	{
		AddInfo(TEXT("This project does not allow unloading gameplay tags in the editor - checking registration only."));
	}

	{
		const FScopedFixtureGameplayTags Scope(UE_PLUGIN_NAME, UE_MODULE_NAME, {
			{ TAG_ScopeFixture_Root,  ScopeFixtureRootName },
			{ TAG_ScopeFixture_Child, ScopeFixtureChildName } });

		TestTrue(TEXT("registered while the scope is open"), IsRegistered(ScopeFixtureChildName));
		TestEqual(TEXT("the bound variable holds the registered tag"), TAG_ScopeFixture_Child.GetTagName(), FName(ScopeFixtureChildName));

		// Ancestry is what most fixtures exist to exercise, and these tags arrive long after the tree was first built. A tag
		// registered that late has to know its parent, or every hierarchy test built on this helper is testing nothing.
		TestTrue(TEXT("a late-registered child matches its parent"), TAG_ScopeFixture_Child.MatchesTag(TAG_ScopeFixture_Root));
		TestTrue(TEXT("and its direct parent is the one registered with it"), TAG_ScopeFixture_Child.RequestDirectParent() == TAG_ScopeFixture_Root);
	}

	TestFalse(TEXT("the bound variables are reset when the scope closes"), TAG_ScopeFixture_Root.IsValid() || TAG_ScopeFixture_Child.IsValid());

	if (bCanUnload)
	{
		TestFalse(TEXT("the child is unregistered once the scope closes"), IsRegistered(ScopeFixtureChildName));
		TestFalse(TEXT("and so is its parent"), IsRegistered(ScopeFixtureRootName));
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

