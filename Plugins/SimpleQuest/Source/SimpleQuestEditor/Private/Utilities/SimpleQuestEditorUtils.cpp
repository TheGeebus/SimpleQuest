// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Utilities/SimpleQuestEditorUtils.h"

#include "Nodes/QuestlineNode_Exit.h"

class UQuestlineNode_Exit;

namespace USimpleQuestEditorUtilities
{
	FString SanitizeQuestlineTagSegment(const FString& InLabel)
	{
		FString Result = InLabel.TrimStartAndEnd();
		for (TCHAR& Ch : Result)
		{
			if (!FChar::IsAlnum(Ch) && Ch != TEXT('_'))
				Ch = TEXT('_');
		}
		return Result;
	}

	TArray<FName> CollectExitOutcomeTagNames(const UEdGraph* Graph)
	{
		TArray<FName> Result;
		if (!Graph) return Result;
		for (const UEdGraphNode* Node : Graph->Nodes)
		{
			if (const UQuestlineNode_Exit* ExitNode = Cast<UQuestlineNode_Exit>(Node))
			{
				if (ExitNode->OutcomeTag.IsValid())
				{
					Result.AddUnique(ExitNode->OutcomeTag.GetTagName());
				}
			}
		}
		return Result;
	}
}
