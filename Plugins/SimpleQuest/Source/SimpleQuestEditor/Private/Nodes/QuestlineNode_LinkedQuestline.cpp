// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Nodes/QuestlineNode_LinkedQuestline.h"

#include "Utilities/SimpleQuestEditorUtils.h"

FText UQuestlineNode_LinkedQuestline::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	if (!LinkedGraph.IsNull()) return FText::FromString(LinkedGraph.GetAssetName());
	if (!NodeLabel.IsEmpty()) return NodeLabel;
	return NSLOCTEXT("SimpleQuestEditor", "LinkedNodeDefaultTitle", "Linked Questline");
}

FLinearColor UQuestlineNode_LinkedQuestline::GetNodeTitleColor() const
{
	return SQ_ED_NODE_LINKED;
}
