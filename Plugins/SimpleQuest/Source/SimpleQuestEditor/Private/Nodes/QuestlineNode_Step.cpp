// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Nodes/QuestlineNode_Step.h"

#include "SimpleQuestLog.h"
#include "Objectives/QuestObjective.h"
#include "Quests/QuestStep.h"
#include "Utilities/SimpleQuestEditorUtils.h"


void UQuestlineNode_Step::AllocateOutcomePins()
{
	if (!ObjectiveClass) return;
	if (const UQuestObjective* CDO = GetDefault<UQuestObjective>(ObjectiveClass))
	{
		for (const FGameplayTag& Tag : CDO->GetPossibleOutcomes())
		{
			if (Tag.IsValid())
			{
				UEdGraphPin* Pin = CreatePin(EGPD_Output, TEXT("QuestOutcome"), Tag.GetTagName());
				if (Pin) Pin->PinFriendlyName = GetTagLeafLabel(Tag.GetTagName());
			}
		}
	}
}

void UQuestlineNode_Step::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	RefreshOutcomePins();
}

void UQuestlineNode_Step::RefreshOutcomePins()
{
	TArray<FName> DesiredNames;
	if (ObjectiveClass)
	{
		if (const UQuestObjective* CDO = GetDefault<UQuestObjective>(ObjectiveClass))
		{
			for (const FGameplayTag& Tag : CDO->GetPossibleOutcomes())
			{
				if (Tag.IsValid()) DesiredNames.Add(Tag.GetTagName());
			}
		}
	}
	SyncPinsByCategory(EGPD_Output, TEXT("QuestOutcome"), DesiredNames, { TEXT("QuestDeactivate"), TEXT("QuestDeactivated") });
}

FText UQuestlineNode_Step::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	if (!NodeLabel.IsEmpty()) return NodeLabel;
	return NSLOCTEXT("SimpleQuestEditor", "LeafNodeDefaultTitle", "Quest Step");
}

FLinearColor UQuestlineNode_Step::GetNodeTitleColor() const
{
	return SQ_ED_NODE_STEP;
}

