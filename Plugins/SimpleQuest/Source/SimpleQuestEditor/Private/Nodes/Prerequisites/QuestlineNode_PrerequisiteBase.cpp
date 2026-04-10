// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Nodes/Prerequisites/QuestlineNode_PrerequisiteBase.h"

#include "Utilities/SimpleQuestEditorUtils.h"

FLinearColor UQuestlineNode_PrerequisiteBase::GetNodeTitleColor() const
{
	return SQ_ED_NODE_PREREQ_GROUP;
}
