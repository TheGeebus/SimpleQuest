// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

// "What just dirtied this asset?" — a question with an exact answer at runtime and none at all from reading source.
// Domain-agnostic on purpose: it watches two engine delegates and dumps a callstack, and knows nothing about tags,
// facts or quests. That is why it sits in Debug/ rather than beside any feature.
//
// TWO ROUTES, AND WATCHING ONE IS A TRAP. UObject::Modify broadcasts OnObjectModified; UPackage::MarkPackageDirty
// broadcasts PackageMarkedDirtyEvent and nothing else. Code that dirties a package directly is invisible to the
// first hook entirely - a trace watching only that came back silent and read as "no cause", which is the worst
// possible failure for a diagnostic.
//
// Off by default; costs nothing until armed.

#include "CoreMinimal.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AssertionMacros.h"
#include "SimpleCoreEditorLog.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	FString GDirtyTraceFilter;
	FDelegateHandle GDirtyObjectHandle;
	FDelegateHandle GDirtyPackageHandle;

	bool DirtyTrace_Matches(const UPackage* Package)
	{
		return Package && !GDirtyTraceFilter.IsEmpty() && Package->GetName().Contains(GDirtyTraceFilter);
	}

	void DirtyTrace_OnObjectModified(UObject* Modified)
	{
		if (!Modified || !DirtyTrace_Matches(Modified->GetPackage())) { return; }

		UE_LOG(LogSimpleCoreEditor, Warning, TEXT("DirtyTrace [Modify]: '%s'"), *Modified->GetPathName());
		FDebug::DumpStackTraceToLog(TEXT("DirtyTrace callstack"), ELogVerbosity::Warning);
	}

	void DirtyTrace_OnPackageMarkedDirty(UPackage* Package, bool bWasDirty)
	{
		if (!DirtyTrace_Matches(Package)) { return; }

		// bWasDirty says whether this call actually CHANGED the state. An already-dirty package gets marked over and
		// over; only the transition is worth a callstack, and saying so keeps the reader from chasing the wrong one.
		UE_LOG(LogSimpleCoreEditor, Warning, TEXT("DirtyTrace [MarkPackageDirty]: '%s' (was already dirty: %s)"),
			*Package->GetName(), bWasDirty ? TEXT("yes") : TEXT("NO - this is the transition"));
		FDebug::DumpStackTraceToLog(TEXT("DirtyTrace callstack"), ELogVerbosity::Warning);
	}

	FAutoConsoleCommand GDirtyTraceCmd(
		TEXT("SimpleCore.TraceAssetDirty"),
		TEXT("Log a callstack whenever a matching package is dirtied, by either route (UObject::Modify or "
			 "MarkPackageDirty). Arg: a substring of the package path. No argument stops tracing."),
		FConsoleCommandWithArgsDelegate::CreateStatic([](const TArray<FString>& Args)
		{
			if (Args.Num() == 0)
			{
				if (GDirtyObjectHandle.IsValid())
				{
					FCoreUObjectDelegates::OnObjectModified.Remove(GDirtyObjectHandle);
					GDirtyObjectHandle.Reset();
				}
				if (GDirtyPackageHandle.IsValid())
				{
					UPackage::PackageMarkedDirtyEvent.Remove(GDirtyPackageHandle);
					GDirtyPackageHandle.Reset();
				}
				GDirtyTraceFilter.Empty();
				UE_LOG(LogSimpleCoreEditor, Log, TEXT("DirtyTrace: off."));
				return;
			}

			GDirtyTraceFilter = Args[0];
			if (!GDirtyObjectHandle.IsValid())
			{
				GDirtyObjectHandle = FCoreUObjectDelegates::OnObjectModified.AddStatic(&DirtyTrace_OnObjectModified);
			}
			if (!GDirtyPackageHandle.IsValid())
			{
				GDirtyPackageHandle = UPackage::PackageMarkedDirtyEvent.AddStatic(&DirtyTrace_OnPackageMarkedDirty);
			}
			UE_LOG(LogSimpleCoreEditor, Log, TEXT("DirtyTrace: watching packages containing '%s' (both routes)."),
				*GDirtyTraceFilter);
		}));
}