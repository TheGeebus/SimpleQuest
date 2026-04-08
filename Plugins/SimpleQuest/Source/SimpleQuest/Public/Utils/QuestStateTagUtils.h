#pragma once
#include "CoreMinimal.h"

namespace QuestStateTagUtils
{
	static const FString Namespace = TEXT("Quest.State.");
	static const FString Leaf_Active = TEXT("Active");
	static const FString Leaf_Succeeded = TEXT("Succeeded");
	static const FString Leaf_Failed = TEXT("Failed");
	static const FString Leaf_PendingGiver = TEXT("PendingGiver");

	// example: Quest.MyQuestline.MyNode becomes Quest.State.MyQuestline.MyNode.Active, encoding state into the tag to send
	// to the World State Subsystem
	inline FName MakeStateFact(FName QuestTagName, const FString& Leaf)
	{
		FString Tag = QuestTagName.ToString();
		if (Tag.StartsWith(TEXT("Quest.")))
		{
			Tag = Namespace + Tag.Mid(6);
		}
		return FName(*(Tag + TEXT(".") + Leaf));
	}
}
