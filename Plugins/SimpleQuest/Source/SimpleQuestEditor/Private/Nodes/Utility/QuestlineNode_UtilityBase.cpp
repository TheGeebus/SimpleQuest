// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Nodes/Utility/QuestlineNode_UtilityBase.h"
#include "SimpleQuestLog.h"
#include "Utilities/SimpleQuestEditorUtils.h"


void UQuestlineNode_UtilityBase::PostLoad()
{
	Super::PostLoad();

	// Wave 5.a migration: "Activate" input → "Enter" on all utility-base descendants (SetBlocked, ClearBlocked, and any future utility nodes).
	// "Activate" reserved for content-node lifecycle participation; utility nodes are portals for execution flow and use "Enter". Forward
	// output unchanged — it's a genuine local passthrough.
	int32 RenamedCount = 0;
	for (UEdGraphPin* Pin : Pins)
	{
		if (Pin && Pin->Direction == EGPD_Input
			&& Pin->PinType.PinCategory == TEXT("QuestActivation")
			&& Pin->PinName == TEXT("Activate"))
		{
			Pin->PinName = TEXT("Enter");
			++RenamedCount;
		}
	}
	if (RenamedCount > 0)
	{
		UE_LOG(LogSimpleQuest, Log,
			TEXT("UQuestlineNode_UtilityBase::PostLoad: migrated '%s' — %d 'Activate' pin(s) renamed to 'Enter'."),
			*GetName(), RenamedCount);
	}
}

void UQuestlineNode_UtilityBase::AllocateDefaultPins()
{
	CreatePin(EGPD_Input,  TEXT("QuestActivation"), TEXT("Enter"));
	CreatePin(EGPD_Output, TEXT("QuestActivation"), TEXT("Forward"));
}

FLinearColor UQuestlineNode_UtilityBase::GetNodeTitleColor() const
{
	return SQ_ED_NODE_UTILITY;
}
