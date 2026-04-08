// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Nodes/Prerequisites/QuestlineNode_PrerequisiteBase.h"

FLinearColor UQuestlineNode_PrerequisiteBase::GetNodeTitleColor() const
{
	return FLinearColor(0.4f, 0.2f, 0.8f); // violet — visually distinct from quest/step nodes
}
