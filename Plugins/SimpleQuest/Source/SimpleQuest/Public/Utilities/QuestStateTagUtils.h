// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SimpleQuestLog.h"
#include "GameplayTagsManager.h"

/**
 * Static utility class for composing the per-node / per-quest state-fact gameplay tag strings used throughout the quest
 * runtime and compiler. Namespaced under SimpleQuest.QuestState.* to keep state facts distinct from the
 * SimpleQuest.Quest.* identity tag hierarchy.
 * Not instantiable — all members are static.
 */
class FQuestStateTagUtils
{
public:
	// ---- Constants ----
	inline static const FString Namespace = TEXT("SimpleQuest.QuestState.");
	inline static const FString Leaf_Live = TEXT("Live");
	inline static const FString Leaf_Completed = TEXT("Completed");
	inline static const FString Leaf_PendingGiver = TEXT("PendingGiver");
	inline static const FString Leaf_Deactivated = TEXT("Deactivated");
	inline static const FString Leaf_Blocked = TEXT("Blocked");

	// ---- Static methods ----

	/** FName overload — used by editor and compiler where tags may not yet be registered. */
	static FName MakeStateFact(FName QuestTagName, const FString& Leaf)
	{
		FString Tag = QuestTagName.ToString();
		if (Tag.StartsWith(TEXT("SimpleQuest.Quest."))) Tag = Namespace + Tag.Mid(18);
		return FName(*(Tag + TEXT(".") + Leaf));
	}

	/** FGameplayTag overload — convenience for runtime code with fully resolved tags. */
	static FName MakeStateFact(FGameplayTag QuestTag, const FString& Leaf)
	{
		return MakeStateFact(QuestTag.GetTagName(), Leaf);
	}

	/**
	 * DEPRECATED — produces a global outcome fact shared by all nodes using the same outcome. Use MakeNodeOutcomeFact
	 * instead for per-node outcome facts.
	 */
	static FName MakeOutcomeFact(FGameplayTag OutcomeTag)
	{
		FString Tag = OutcomeTag.GetTagName().ToString();
		if (Tag.StartsWith(TEXT("SimpleQuest.Quest."))) Tag = Namespace + Tag.Mid(18);
		return FName(*Tag);
	}

	/**
	 * Format a per-node path fact: SimpleQuest.QuestState.<NodePath>.Path.<PathLeaf>
	 *
	 * Path identity is an FName that uniquely identifies a completion route through this node. For static K2
	 * placements the identity is the outcome tag's full FName (preserving existing pin-name semantics);
	 * for dynamic placements it's a sanitized author-supplied path name. The "SimpleQuest.QuestOutcome." prefix
	 * is stripped when present so the leaf reads cleanly under .Path.<...>; bare path identities pass through
	 * unmodified.
	 *
	 * @param NodeTagName    The tag describing this node's position in a graph hierarchy
	 *                       (e.g. SimpleQuest.Quest.Act1.Chapter3.RecruitAllies).
	 * @param PathIdentity   FName routing identifier for the completion path
	 *                       (e.g. SimpleQuest.QuestOutcome.HireMercenaries — static; or "DynamicVictory" — dynamic).
	 * @returns              A fact tag encoding the node + path
	 *                       (e.g. SimpleQuest.QuestState.Act1.Chapter3.RecruitAllies.Path.HireMercenaries).
	 */
	static FName MakeNodePathFact(FName NodeTagName, FName PathIdentity)
	{
		if (PathIdentity.IsNone()) return NAME_None;

		FString NodeStr = NodeTagName.ToString();
		if (NodeStr.StartsWith(TEXT("SimpleQuest.Quest.")))
			NodeStr = Namespace + NodeStr.Mid(18);

		FString PathStr = PathIdentity.ToString();
		static const FString OutcomePrefix = TEXT("SimpleQuest.QuestOutcome.");
		if (PathStr.StartsWith(OutcomePrefix))
			PathStr = PathStr.RightChop(OutcomePrefix.Len());

		return FName(*(NodeStr + TEXT(".Path.") + PathStr));
	}

	/**
	 * Per-quest entry path fact: SimpleQuest.QuestState.<QuestPath>.EntryPath.<PathLeaf>. Set when a Quest
	 * node is activated via a specific entry path identity (matching the upstream node's PathIdentity that
	 * caused this Quest's activation).
	 */
	static FName MakeEntryPathFact(FName NodeTagName, FName PathIdentity)
	{
		if (PathIdentity.IsNone()) return NAME_None;

		FString NodeStr = NodeTagName.ToString();
		if (NodeStr.StartsWith(TEXT("SimpleQuest.Quest.")))
			NodeStr = Namespace + NodeStr.Mid(18);

		FString PathStr = PathIdentity.ToString();
		static const FString OutcomePrefix = TEXT("SimpleQuest.QuestOutcome.");
		if (PathStr.StartsWith(OutcomePrefix))
			PathStr = PathStr.RightChop(OutcomePrefix.Len());

		return FName(*(NodeStr + TEXT(".EntryPath.") + PathStr));
	}

	/**
	 * True iff Tag is well-formed AND currently registered in the runtime UGameplayTagsManager. Defensive — an FGameplayTag
	 * can pass IsValid() (non-NAME_None) while holding a stale reference whose source compiled tag no longer exists in the
	 * registry. Stale tags ensure inside UE's FGameplayTag::MatchesAny whenever any tag-container operation iterates them,
	 * so this helper is the foundation for the container-sanitizing accessors on the three quest components.
	 */
	static bool IsTagRegisteredInRuntime(const FGameplayTag& Tag)
	{
		if (!Tag.IsValid()) return false;
		return UGameplayTagsManager::Get().RequestGameplayTag(Tag.GetTagName(), /*ErrorIfNotFound*/ false).IsValid();
	}

	/**
	 * Returns a copy of Container with every unregistered (stale) tag filtered out. O(N) in container size; a Warning is
	 * logged per stale tag so designers see exactly which references need cleanup via the Stale Quest Tags panel. Intended
	 * for BP callsites that feed the result into tag-library operations (Filter, HasAny, MatchesAny) that assert on stale.
	 */
	static FGameplayTagContainer FilterToRegisteredTags(const FGameplayTagContainer& Container, const FString& ContextLabel)
	{
		FGameplayTagContainer Result;
		for (const FGameplayTag& Tag : Container)
		{
			if (IsTagRegisteredInRuntime(Tag)) { Result.AddTag(Tag); continue; }
			UE_LOG(LogSimpleQuest, Warning,
				TEXT("%s : filtering stale tag '%s' — no longer registered. ")
				TEXT("Use Stale Quest Tags (Window → Developer Tools → Debug) to clean up."),
				*ContextLabel, *Tag.ToString());
		}
		return Result;
	}

private:
	FQuestStateTagUtils() = delete;
};