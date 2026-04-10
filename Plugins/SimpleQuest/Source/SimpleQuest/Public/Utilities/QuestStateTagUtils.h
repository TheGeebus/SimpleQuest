// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace QuestStateTagUtils
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

	// Quest.MyLine.MyNode.Outcome.BetrayedGuild → Quest.State.MyLine.MyNode.Outcome.BetrayedGuild
	inline FName MakeOutcomeFact(FGameplayTag OutcomeTag)
	{
		FString Tag = OutcomeTag.GetTagName().ToString();
		if (Tag.StartsWith(TEXT("Quest."))) Tag = Namespace + Tag.Mid(6);
		return FName(*Tag);
	}
}
