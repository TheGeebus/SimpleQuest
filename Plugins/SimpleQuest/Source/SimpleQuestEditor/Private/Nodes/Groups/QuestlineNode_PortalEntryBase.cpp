// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Nodes/Groups/QuestlineNode_PortalEntryBase.h"
#include "Utilities/SimpleQuestEditorUtils.h"

FLinearColor UQuestlineNode_PortalEntryBase::GetNodeTitleColor() const
{
	return SQ_ED_NODE_ACTIVATE_GROUP;
}