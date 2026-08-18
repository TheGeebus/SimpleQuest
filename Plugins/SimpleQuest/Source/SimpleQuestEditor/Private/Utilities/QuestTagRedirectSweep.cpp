// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

// The inverse of FSimpleQuestEditorUtilities::WriteGameplayTagRedirects, and it lives beside it for that reason:
// creating and retiring the same artifact is one lifecycle, and the retirer is looked for next to the writer.
//
// WHY RETIREMENT MATTERS AT ALL: MarkDirtyOnRedirectedTagLoad dirties any asset holding a tag that appears as a
// redirect TARGET, because it cannot tell "healed on load" from "has been correct for months" - its own comment
// concedes this. So a spent redirect nags forever. Deleting it is the only thing that ends the churn, and the
// heuristic short-circuits entirely once the redirect list is empty.
//
// A REDIRECT IS DEAD WHEN NO PACKAGE ON DISK STILL CONTAINS ITS OldTagName. That is a statement about BYTES, not
// about loaded objects: loading HEALS a tag, so an in-memory asset already reports the new name and can never tell
// you whether the old one is still written down.
//
// EXACT MATCHING, NOT SUBSTRING. An FName in a package's name table is serialized length-prefixed and
// null-terminated - int32(Len+1), the ANSI characters, then \0 - so that byte pattern cannot match "Quest.Outcome"
// inside "Quest.Outcome.Solved". A substring scan would be harmless for resaving and FATAL for retirement, which is
// the one decision this exists to support.
//
// PLAN FIRST, matching ImportQuestline: scanning is the default, mutating is opted into, and DELETING the ini lines
// stays manual - the report names exactly which are retirable, and a config edit is cheap to review by hand.

#include "CoreMinimal.h"
#include "GameplayTagsSettings.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "SimpleQuestLog.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Utilities/SimpleQuestEditorUtils.h"

namespace
{
	/** Every uncooked package the sweep is answerable for: the project's content plus every plugin that ships any. */
	TArray<FString> GatherContentRoots()
	{
		TArray<FString> Roots;
		Roots.Add(FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir()));
		// PROJECT plugins only. GetEnabledPluginsWithContent() includes ENGINE plugins, whose content cannot contain a
		// tag this project renamed - scanning them is pure cost, and they are the bulk of the bytes.
		for (const TSharedRef<IPlugin>& Plugin : IPluginManager::Get().GetEnabledPluginsWithContent())
		{
			if (Plugin->GetType() == EPluginType::Project)
			{
				Roots.Add(FPaths::ConvertRelativePathToFull(Plugin->GetContentDir()));
			}
		}
		return Roots;
	}

	/** The exact serialized form of one FName: int32(Len+1) little-endian, the ANSI characters, then the terminator. */
	TArray<uint8> MakeNamePattern(const FName Name)
	{
		const FString AsString = Name.ToString();
		const int32 Serialized = AsString.Len() + 1;

		TArray<uint8> Pattern;
		Pattern.Reserve(sizeof(int32) + Serialized);
		Pattern.Append(reinterpret_cast<const uint8*>(&Serialized), sizeof(int32));
		for (TCHAR Ch : AsString) { Pattern.Add(static_cast<uint8>(Ch)); }
		Pattern.Add(0);
		return Pattern;
	}

	/**
	 * Every pattern at once, in ONE pass over the file. The per-pattern full scan this replaced cost
	 * bytes x patterns - 48 billion comparisons over a 400MB corpus with 119 redirects, which reads as a hung editor.
	 * Each pattern starts with an int32 LENGTH, so a length that no pattern uses rejects the offset with a single
	 * array lookup, and binary data rejects essentially everywhere.
	 */
	void FindPatternsInBuffer(const TArray<uint8>& Bytes, const TArray<TArray<uint8>>& Patterns,
		const TArray<TArray<int32>>& PatternsByLength, TArray<bool>& OutFound)
	{
		if (Bytes.Num() < sizeof(int32)) { return; }

		const int32 Last = Bytes.Num() - sizeof(int32);
		for (int32 Offset = 0; Offset <= Last; ++Offset)
		{
			int32 Len = 0;
			FMemory::Memcpy(&Len, Bytes.GetData() + Offset, sizeof(int32));
			if (Len <= 0 || Len >= PatternsByLength.Num()) { continue; }

			for (const int32 Index : PatternsByLength[Len])
			{
				if (OutFound[Index]) { continue; }   // already known present; nothing left to learn

				const TArray<uint8>& Pattern = Patterns[Index];
				if (Offset + Pattern.Num() <= Bytes.Num()
					&& FMemory::Memcmp(Bytes.GetData() + Offset, Pattern.GetData(), Pattern.Num()) == 0)
				{
					OutFound[Index] = true;
				}
			}
		}
	}

	/**
	 * Every tag the manager currently knows, by name. Deliberately redirect-BLIND: RequestGameplayTag applies the redirect
	 * before it looks anything up, so asking it about an OldTagName answers a question about the TARGET. Walking the tree is
	 * the only way to ask whether one exact name is live - which is the whole question the INVERTED verdict turns on.
	 */
	TSet<FName> GatherRegisteredTagNames()
	{
		FGameplayTagContainer All;
		UGameplayTagsManager::Get().RequestAllGameplayTags(All, false);

		TSet<FName> Names;
		Names.Reserve(All.Num());
		for (const FGameplayTag& Tag : All)
		{
			Names.Add(Tag.GetTagName());
		}
		return Names;
	}


	void ScanTagRedirectsCmd(const TArray<FString>& Args)
	{
		const bool bApply = Args.ContainsByPredicate(
			[](const FString& A) { return A.Equals(TEXT("--apply"), ESearchCase::IgnoreCase); });
		const bool bPrune = Args.ContainsByPredicate(
			[](const FString& A) { return A.Equals(TEXT("--prune"), ESearchCase::IgnoreCase); });

		// Never both in one run. --apply CHANGES what is on disk, so every RETIRABLE verdict in this run predates the
		// resave and is stale the moment it finishes. Pruning on it would delete redirects proven spent by data that
		// no longer describes the project.
		if (bApply && bPrune)
		{
			UE_LOG(LogSimpleQuestCompiler, Error, TEXT("ScanTagRedirects: --apply and --prune cannot run together. "
				"Apply, restart, scan again, then prune on the fresh result."));
			return;
		}

		const UGameplayTagsSettings* Settings = GetDefault<UGameplayTagsSettings>();
		if (!Settings || Settings->GameplayTagRedirects.Num() == 0)
		{
			UE_LOG(LogSimpleQuestCompiler, Log, TEXT("ScanTagRedirects: no gameplay tag redirects configured - nothing to retire."));
			return;
		}

		if (bApply)
		{
			// Healing happens at DESERIALIZE. A package already resident from before a redirect existed still holds the
			// OLD name in memory, and resaving it would write that back - the sweep would never converge. A package
			// loaded in a fresh session cannot be in that state, because the ini is read before any asset loads.
			UE_LOG(LogSimpleQuestCompiler, Warning, TEXT("ScanTagRedirects --apply: run this in a FRESHLY RESTARTED editor. "
				"Any package already loaded from before a redirect was added still holds the old name in memory, and "
				"resaving it would persist the old name rather than heal it."));
		}

		// One pattern per redirect, built once. The FILE loop is the outer one so each package is read from disk exactly
		// once and tested against every pattern, rather than re-reading the whole corpus per redirect.
		TArray<TArray<uint8>> Patterns;
		Patterns.Reserve(Settings->GameplayTagRedirects.Num());
		for (const FGameplayTagRedirect& Redirect : Settings->GameplayTagRedirects)
		{
			Patterns.Add(MakeNamePattern(Redirect.OldTagName));
		}

		// Bucketed by serialized length so FindPatternsInBuffer can reject an offset with one array lookup. 256 covers
		// any sane tag name; anything longer is REPORTED rather than silently skipped, because an unchecked pattern
		// would come back RETIRABLE - and a false RETIRABLE is the verdict that deletes a redirect still in use.
		TArray<TArray<int32>> PatternsByLength;
		PatternsByLength.SetNum(256);
		for (int32 Index = 0; Index < Patterns.Num(); ++Index)
		{
			int32 Len = 0;
			FMemory::Memcpy(&Len, Patterns[Index].GetData(), sizeof(int32));
			if (Len > 0 && Len < PatternsByLength.Num())
			{
				PatternsByLength[Len].Add(Index);
			}
			else
			{
				UE_LOG(LogSimpleQuestCompiler, Warning, TEXT("ScanTagRedirects: '%s' is too long to scan for - it would "
					"be reported RETIRABLE without ever being checked. Do not delete it on this scan's word."),
					*Settings->GameplayTagRedirects[Index].OldTagName.ToString());
			}
		}

		TArray<FString> PackageFiles;
		for (const FString& Root : GatherContentRoots())
		{
			IFileManager::Get().FindFilesRecursive(PackageFiles, *Root, TEXT("*.uasset"), true, false, false);
			IFileManager::Get().FindFilesRecursive(PackageFiles, *Root, TEXT("*.umap"),   true, false, false);
		}

		TArray<TArray<FString>> HitsByRedirect;
		HitsByRedirect.SetNum(Settings->GameplayTagRedirects.Num());
		TSet<FString> FilesNeedingResave;
		int32 Unreadable = 0;

		TArray<bool> Found;
		for (int32 FileIndex = 0; FileIndex < PackageFiles.Num(); ++FileIndex)
		{
			// Progress, because this walks every package synchronously on the game thread and a silent long run is
			// indistinguishable from a hang - which is exactly how the first version of this presented.
			if ((FileIndex % 100) == 0)
			{
				UE_LOG(LogSimpleQuestCompiler, Log, TEXT("ScanTagRedirects: %d / %d packages..."),
					FileIndex, PackageFiles.Num());
			}

			const FString& File = PackageFiles[FileIndex];

			TArray<uint8> Bytes;
			if (!FFileHelper::LoadFileToArray(Bytes, *File))
			{
				// Counted, never ignored: a package the sweep could not read is one it cannot VOUCH for, and retirement
				// is a claim about EVERY package. One unreadable file makes "zero hits" an unsafe conclusion.
				++Unreadable;
				UE_LOG(LogSimpleQuestCompiler, Warning, TEXT("ScanTagRedirects: could not read '%s'."), *File);
				continue;
			}

			// ONE pass for all patterns, rather than one pass per pattern.
			Found.Reset();
			Found.SetNumZeroed(Patterns.Num());
			FindPatternsInBuffer(Bytes, Patterns, PatternsByLength, Found);

			for (int32 Index = 0; Index < Patterns.Num(); ++Index)
			{
				if (Found[Index])
				{
					HitsByRedirect[Index].Add(File);
					FilesNeedingResave.Add(File);
				}
			}
		}

		// A redirect whose OLD name is still a REGISTERED tag is not "in use" - it is INVERTED, and it breaks the very tag it
		// names. RequestGameplayTag applies redirects BEFORE the registry lookup, so the live name resolves to the target
		// instead of to itself: to an EMPTY tag when the target is registered nowhere (the node then activates through the
		// FName routing tables while carrying an invalid ContextualTag - no state facts, no deactivation subscription), or to
		// a different live tag when it does exist. Both are defects, and resaving the holder fixes neither: the package
		// contains its own current identity, not a stale one.
		//
		// "Move a node, then move it back" produces exactly this, and nothing else will tell you. The engine never validates
		// redirect targets, and a stale redirect actively SUPPRESSES the "Invalid GameplayTag" warning that would surface it.
		const TSet<FName> RegisteredTags = GatherRegisteredTagNames();

		int32 Retirable = 0;
		int32 Inverted = 0;
		for (int32 Index = 0; Index < Settings->GameplayTagRedirects.Num(); ++Index)
		{
			const FGameplayTagRedirect& Redirect = Settings->GameplayTagRedirects[Index];
			const TArray<FString>& Hits = HitsByRedirect[Index];

			// Tested BEFORE the hit count, because an inverted redirect with no hits would otherwise read as RETIRABLE - the
			// right action for the wrong reason, and the reason is the part worth knowing.
			if (RegisteredTags.Contains(Redirect.OldTagName))
			{
				++Inverted;
				const bool bTargetLive = RegisteredTags.Contains(Redirect.NewTagName);
				UE_LOG(LogSimpleQuestCompiler, Warning, TEXT("  INVERTED   %s -> %s"),
					*Redirect.OldTagName.ToString(), *Redirect.NewTagName.ToString());
				UE_LOG(LogSimpleQuestCompiler, Warning,
					TEXT("               '%s' is a REGISTERED tag, so this redirect points away from a live name%s. Delete "
						"the line from Config/DefaultGameplayTags.ini - do NOT resave the holder(s)."),
					*Redirect.OldTagName.ToString(),
					bTargetLive
						? TEXT(", silently resolving every reference to a DIFFERENT live tag")
						: TEXT(", and the target is registered NOWHERE - every reference resolves to an INVALID tag"));
				for (const FString& Hit : Hits)
				{
					UE_LOG(LogSimpleQuestCompiler, Warning, TEXT("               %s"), *Hit);
				}
				continue;
			}

			if (Hits.Num() == 0)
			{
				++Retirable;
				UE_LOG(LogSimpleQuestCompiler, Log, TEXT("  RETIRABLE  %s -> %s"),
					*Redirect.OldTagName.ToString(), *Redirect.NewTagName.ToString());
				continue;
			}

			UE_LOG(LogSimpleQuestCompiler, Log, TEXT("  IN USE     %s -> %s  (%d package(s))"),
				*Redirect.OldTagName.ToString(), *Redirect.NewTagName.ToString(), Hits.Num());
			for (const FString& Hit : Hits)
			{
				// At Log, not Verbose: this IS the plan, and a plan nobody can read by default is not plan-first.
				UE_LOG(LogSimpleQuestCompiler, Log, TEXT("               %s"), *Hit);
			}
		}

		// Rebuild from NON-inverted hits only. FilesNeedingResave is accumulated during the byte scan, before anything has
		// been classified - so an inverted redirect's holder lands in it, and the summary then recommends --apply two lines
		// after the verdict said not to resave that very package. A package appearing ONLY under an inverted redirect needs
		// nothing done to it: it holds its own current identity. One that ALSO holds a genuinely stale name under some other
		// redirect must stay, which is why this rebuilds the set rather than subtracting from it.
		FilesNeedingResave.Reset();
		for (int32 Index = 0; Index < Settings->GameplayTagRedirects.Num(); ++Index)
		{
			if (RegisteredTags.Contains(Settings->GameplayTagRedirects[Index].OldTagName)) { continue; }
			for (const FString& Hit : HitsByRedirect[Index]) { FilesNeedingResave.Add(Hit); }
		}

		UE_LOG(LogSimpleQuestCompiler, Log, TEXT("ScanTagRedirects: %d redirect(s) - %d RETIRABLE, %d INVERTED, %d still in "
			"use across %d package(s) of %d scanned.%s"),
			Settings->GameplayTagRedirects.Num(),
			Retirable,
			Inverted,
			Settings->GameplayTagRedirects.Num() - Retirable - Inverted,
			FilesNeedingResave.Num(),
			PackageFiles.Num(),
			Unreadable > 0
				? *FString::Printf(TEXT(" %d UNREADABLE - retirement is NOT safe until those are explained."), Unreadable)
				: TEXT(""));

		if (bPrune)
		{
			if (Unreadable > 0)
			{
				UE_LOG(LogSimpleQuestCompiler, Error, TEXT("ScanTagRedirects: refusing to prune - %d package(s) could not "
					"be read, so RETIRABLE is not a claim this scan can back."), Unreadable);
				return;
			}

			TSet<FName> ToRemove;
			for (int32 Index = 0; Index < Settings->GameplayTagRedirects.Num(); ++Index)
			{
				const FName OldName = Settings->GameplayTagRedirects[Index].OldTagName;
				// INVERTED is never pruned automatically. Deleting it IS the right remedy, but for a different reason than
				// "nothing references it" - and a redirect that turned out to point away from a live tag deserves a human
				// look at HOW it got written before the evidence leaves the ini.
				if (RegisteredTags.Contains(OldName)) { continue; }
				if (HitsByRedirect[Index].Num() == 0) { ToRemove.Add(OldName); }
			}

			const int32 RemovedCount = FSimpleQuestEditorUtilities::RemoveGameplayTagRedirects(ToRemove);
			UE_LOG(LogSimpleQuestCompiler, Log, TEXT("ScanTagRedirects: removed %d retirable redirect(s) from "
				"Config/DefaultGameplayTags.ini. Restart the editor for the change to take effect, and review the diff - "
				"the file is tracked, so `git diff` shows exactly what went.%s"), RemovedCount,
				Inverted > 0
					? *FString::Printf(TEXT(" %d INVERTED redirect(s) left in place - delete those by hand after reading the "
						"report above."), Inverted)
					: TEXT(""));
		}

		if (!bApply)
		{
			if (FilesNeedingResave.Num() > 0)
			{
				UE_LOG(LogSimpleQuestCompiler, Log, TEXT("ScanTagRedirects: re-run with --apply to resave those packages, "
					"then scan again. A redirect reporting RETIRABLE on a clean scan is safe to delete."));
			}
			UE_LOG(LogSimpleQuestCompiler, Log, TEXT("ScanTagRedirects: re-run with --prune to delete the RETIRABLE "
				"entries from Config/DefaultGameplayTags.ini."));
			return;
		}

		// APPLY. Loading is what heals the tag (FGameplayTag::PostSerialize); this only has to make the healed value
		// PERSIST. Nothing is edited - the save IS the whole operation.
		int32 Saved = 0, Failed = 0;
		for (const FString& File : FilesNeedingResave)
		{
			FString PackageName;
			if (!FPackageName::TryConvertFilenameToLongPackageName(File, PackageName))
			{
				++Failed;
				UE_LOG(LogSimpleQuestCompiler, Warning, TEXT("ScanTagRedirects: '%s' is not under a mounted content root - skipped."), *File);
				continue;
			}

			UPackage* Package = LoadPackage(nullptr, *PackageName, LOAD_None);
			if (!Package)
			{
				++Failed;
				UE_LOG(LogSimpleQuestCompiler, Warning, TEXT("ScanTagRedirects: could not load '%s' - skipped."), *PackageName);
				continue;
			}

			Package->MarkPackageDirty();
			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
			if (UPackage::SavePackage(Package, nullptr, *File, SaveArgs))
			{
				++Saved;
			}
			else
			{
				++Failed;
				UE_LOG(LogSimpleQuestCompiler, Warning, TEXT("ScanTagRedirects: failed to save '%s'."), *PackageName);
			}
		}

		UE_LOG(LogSimpleQuestCompiler, Log, TEXT("ScanTagRedirects: resaved %d package(s), %d failed. Re-run the scan to "
			"confirm which redirects are now retirable."), Saved, Failed);
	}

	FAutoConsoleCommand GScanTagRedirectsCmd(
		TEXT("SimpleQuest.ScanTagRedirects"),
		TEXT("Report which gameplay tag redirects are still needed, by scanning every package's name table ON DISK for "
			 "their OldTagName - loading cannot answer this, because loading heals the tag. A redirect with zero hits "
			 "is safe to delete from Config/DefaultGameplayTags.ini. Pass --apply to resave the packages still holding "
			 "old names (freshly restarted editor only), then scan again to confirm."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&ScanTagRedirectsCmd));
}
