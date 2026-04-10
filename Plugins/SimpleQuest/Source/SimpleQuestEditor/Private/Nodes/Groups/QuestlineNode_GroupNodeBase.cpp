// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Nodes/Groups/QuestlineNode_GroupNodeBase.h"

#include "Utilities/SimpleQuestEditorUtils.h"

FLinearColor UQuestlineNode_GroupNodeBase::GetNodeTitleColor() const
{
	return SQ_ED_NODE_ACTIVATE_GROUP;
}
