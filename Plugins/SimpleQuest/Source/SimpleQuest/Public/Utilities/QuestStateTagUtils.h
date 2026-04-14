// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace UQuestStateTagUtils
{
	static const FString Namespace			 = TEXT("QuestState.");
	static const FString Leaf_Active		 = TEXT("Active");
	static const FString Leaf_Completed		 = TEXT("Completed");
	static const FString Leaf_PendingGiver	 = TEXT("PendingGiver");
	static const FString Leaf_Deactivated	 = TEXT("Deactivated");
	static const FString Leaf_Blocked		 = TEXT("Blocked");

	// FName overload — used by editor and compiler where tags may not yet be registered
	inline FName MakeStateFact(FName QuestTagName, const FString& Leaf)
	{
		FString Tag = QuestTagName.ToString();
		if (Tag.StartsWith(TEXT("Quest."))) Tag = Namespace + Tag.Mid(6);
		return FName(*(Tag + TEXT(".") + Leaf));
	}

	// FGameplayTag overload — convenience for runtime code with fully resolved tags
	inline FName MakeStateFact(FGameplayTag QuestTag, const FString& Leaf)
	{
		return MakeStateFact(QuestTag.GetTagName(), Leaf);
	}

	// DEPRECATED — produces a global outcome fact shared by all nodes using the same outcome.
	// Use MakeNodeOutcomeFact instead for per-node outcome facts.
	inline FName MakeOutcomeFact(FGameplayTag OutcomeTag)
	{
		FString Tag = OutcomeTag.GetTagName().ToString();
		if (Tag.StartsWith(TEXT("Quest."))) Tag = Namespace + Tag.Mid(6);
		return FName(*Tag);
	}

	/**
	 * Format a per-node outcome fact in the form of: Quest.State.<NodePath>.Outcome.<OutcomeLeaf>
	 * 
	 * @param NodeTagName the tag describing this node's position in a graph hierarchy.								<br>
	 * e.g. Quest.Act1.Chapter3.RecruitAllies																		<br>
	 * 
	 * @param OutcomeTag the tag describing a given outcome	for a quest or step.									<br>
	 * e.g. Quest.Outcome.HireMercenaries																			<br>
	 * 
	 * @returns a fact tag that encodes the graph context along with the outcome.									<br>
	 * e.g. Quest.State.Act1.Chapter3.RecruitAllies.Outcome.HireMercenaries
	 */

	inline FName MakeNodeOutcomeFact(FName NodeTagName, FGameplayTag OutcomeTag)
	{
		// Build per-node state prefix: Quest.NewTest.Step_3 → Quest.State.NewTest.Step_3
		FString NodeStr = NodeTagName.ToString();
		if (NodeStr.StartsWith(TEXT("Quest.")))
			NodeStr = Namespace + NodeStr.Mid(6);

		// Extract the Outcome.* suffix from the outcome tag
		FString OutcomeStr = OutcomeTag.GetTagName().ToString();
		int32 OutcomePos = OutcomeStr.Find(TEXT("Outcome."));
		if (OutcomePos == INDEX_NONE) return NAME_None;

		FString OutcomeSuffix = OutcomeStr.Mid(OutcomePos); // "Outcome.Reached"

		return FName(*(NodeStr + TEXT(".") + OutcomeSuffix));
	}

	// Per-quest entry outcome fact: Quest.State.<QuestPath>.EntryOutcome.<OutcomeLeaf>
	// Set when a Quest node is activated via a specific IncomingOutcomeTag.
	// e.g. NodeTagName = Quest.MainQuest.Quest_1, OutcomeTag = Quest.Outcome.Reached
	//   → Quest.State.MainQuest.Quest_1.EntryOutcome.Reached
	inline FName MakeEntryOutcomeFact(FName NodeTagName, FGameplayTag OutcomeTag)
	{
		FString NodeStr = NodeTagName.ToString();
		if (NodeStr.StartsWith(TEXT("Quest.")))
			NodeStr = Namespace + NodeStr.Mid(6);

		FString OutcomeStr = OutcomeTag.GetTagName().ToString();
		int32 OutcomePos = OutcomeStr.Find(TEXT("Outcome."));
		if (OutcomePos == INDEX_NONE) return NAME_None;

		FString OutcomeLeaf = OutcomeStr.Mid(OutcomePos + 8); // after "Outcome."
		return FName(*(NodeStr + TEXT(".EntryOutcome.") + OutcomeLeaf));
	}
}
