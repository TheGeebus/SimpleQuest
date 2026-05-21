// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Nodes/Groups/QuestlineNode_PortalEntryBase.h"
#include "Utilities/SimpleQuestEditorUtils.h"

FLinearColor UQuestlineNode_PortalEntryBase::GetNodeTitleColor() const
{
	return SQ_ED_NODE_ACTIVATE_GROUP;
}