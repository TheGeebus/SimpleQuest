// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace UQuestStateTagUtils
{
	static const FString Namespace			 = TEXT("Quest.State.");
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
	
	// Per-node outcome fact: Quest.State.<NodePath>.Outcome.<OutcomeLeaf>
	// e.g. NodeTagName = Quest.NewTest.Step_3, OutcomeTag = Quest.BuiltIn.GoTo.Outcome.Reached → Quest.State.NewTest.Step_3.Outcome.Reached
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
}
