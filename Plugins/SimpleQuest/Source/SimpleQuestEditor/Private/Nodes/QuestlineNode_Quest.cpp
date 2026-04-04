// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Nodes/QuestlineNode_Quest.h"

FText UQuestlineNode_Quest::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	if (!NodeLabel.IsEmpty()) return NodeLabel;
	return NSLOCTEXT("SimpleQuestEditor", "QuestNodeDefaultTitle", "Quest");
}


