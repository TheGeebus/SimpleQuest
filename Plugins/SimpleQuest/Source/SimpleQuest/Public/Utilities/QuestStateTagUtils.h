// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SimpleQuestLog.h"
#include "GameplayTagsManager.h"

/**
 * Static utility class for composing the per-node / per-quest state-fact gameplay tag strings used throughout the quest
 * runtime and compiler. Namespaced under QuestState.* to keep state facts distinct from the Quest.* identity tag hierarchy.
 * Not instantiable — all members are static.
 */
class FQuestStateTagUtils
{
public:
	// ---- Constants ----
	inline static const FString Namespace = TEXT("QuestState.");
	inline static const FString Leaf_Active = TEXT("Active");
	inline static const FString Leaf_Completed = TEXT("Completed");
	inline static const FString Leaf_PendingGiver = TEXT("PendingGiver");
	inline static const FString Leaf_Deactivated = TEXT("Deactivated");
	inline static const FString Leaf_Blocked = TEXT("Blocked");

	// ---- Static methods ----

	/** FName overload — used by editor and compiler where tags may not yet be registered. */
	static FName MakeStateFact(FName QuestTagName, const FString& Leaf)
	{
		FString Tag = QuestTagName.ToString();
		if (Tag.StartsWith(TEXT("Quest."))) Tag = Namespace + Tag.Mid(6);
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
		if (Tag.StartsWith(TEXT("Quest."))) Tag = Namespace + Tag.Mid(6);
		return FName(*Tag);
	}

	/**
	 * Format a per-node outcome fact: Quest.State.<NodePath>.Outcome.<OutcomeLeaf>
	 *
	 * @param NodeTagName  The tag describing this node's position in a graph hierarchy
	 *                     (e.g. Quest.Act1.Chapter3.RecruitAllies).
	 * @param OutcomeTag   The tag describing a given outcome for a quest or step
	 *                     (e.g. Quest.Outcome.HireMercenaries).
	 * @returns            A fact tag encoding the graph context along with the outcome
	 *                     (e.g. Quest.State.Act1.Chapter3.RecruitAllies.Outcome.HireMercenaries).
	 */
	static FName MakeNodeOutcomeFact(FName NodeTagName, FGameplayTag OutcomeTag)
	{
		FString NodeStr = NodeTagName.ToString();
		if (NodeStr.StartsWith(TEXT("Quest.")))
			NodeStr = Namespace + NodeStr.Mid(6);

		FString OutcomeStr = OutcomeTag.GetTagName().ToString();
		int32 OutcomePos = OutcomeStr.Find(TEXT("Outcome."));
		if (OutcomePos == INDEX_NONE) return NAME_None;

		FString OutcomeSuffix = OutcomeStr.Mid(OutcomePos);
		return FName(*(NodeStr + TEXT(".") + OutcomeSuffix));
	}

	/**
	 * Per-quest entry outcome fact: Quest.State.<QuestPath>.EntryOutcome.<OutcomeLeaf>. Set when a Quest node is activated
	 * via a specific IncomingOutcomeTag.
	 */
	static FName MakeEntryOutcomeFact(FName NodeTagName, FGameplayTag OutcomeTag)
	{
		FString NodeStr = NodeTagName.ToString();
		if (NodeStr.StartsWith(TEXT("Quest.")))
			NodeStr = Namespace + NodeStr.Mid(6);

		FString OutcomeStr = OutcomeTag.GetTagName().ToString();
		int32 OutcomePos = OutcomeStr.Find(TEXT("Outcome."));
		if (OutcomePos == INDEX_NONE) return NAME_None;

		FString OutcomeLeaf = OutcomeStr.Mid(OutcomePos + 8);
		return FName(*(NodeStr + TEXT(".EntryOutcome.") + OutcomeLeaf));
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