// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Nodes/QuestlineNode_Step.h"

#include "Objectives/QuestObjective.h"
#include "Quests/QuestStep.h"
#include "Utilities/SimpleQuestEditorUtils.h"

void UQuestlineNode_Step::AllocateDefaultPins()
{
	// Outcome pins derived from objective CDO
	if (ObjectiveClass)
	{
		if (const UQuestObjective* CDO = GetDefault<UQuestObjective>(ObjectiveClass))
		{
			for (const FGameplayTag& OutcomeTag : CDO->GetPossibleOutcomes())
			{
				if (OutcomeTag.IsValid()) CreatePin(EGPD_Output, TEXT("QuestOutcome"), OutcomeTag.GetTagName());
			}
		}
	}
	
	// Base pins from ContentBase (Activate, Prerequisites, Any Outcome)
	Super::AllocateDefaultPins();
}

void UQuestlineNode_Step::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UQuestlineNode_Step, ObjectiveClass))	ReconstructNode();
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
