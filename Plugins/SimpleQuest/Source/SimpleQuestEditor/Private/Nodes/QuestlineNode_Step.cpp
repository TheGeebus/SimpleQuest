// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Nodes/QuestlineNode_Step.h"

#include "SimpleQuestLog.h"
#include "Objectives/QuestObjective.h"
#include "Quests/QuestStep.h"
#include "Utilities/SimpleQuestEditorUtils.h"


void UQuestlineNode_Step::AllocateOutcomePins()
{
	if (ObjectiveClass.IsNull()) return;

	TArray<FName> Paths = FSimpleQuestEditorUtilities::DiscoverObjectivePaths(ObjectiveClass.LoadSynchronous());
	for (const FName& PathIdentity : Paths)
	{
		if (PathIdentity.IsNone()) continue;
		UEdGraphPin* Pin = CreatePin(EGPD_Output, TEXT("QuestOutcome"), PathIdentity);
		if (Pin) Pin->PinFriendlyName = GetTagLeafLabel(PathIdentity);
	}
}

void UQuestlineNode_Step::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	RefreshOutcomePins();

	// Always rebuild the custom Slate widget — target lists, border, etc. may depend on properties that don't affect pins.
	if (UEdGraph* Graph = GetGraph())
	{
		Graph->NotifyGraphChanged();
	}
}

void UQuestlineNode_Step::RefreshOutcomePins()
{
	TArray<FName> DesiredNames;
	if (!ObjectiveClass.IsNull())
	{
		DesiredNames = FSimpleQuestEditorUtilities::DiscoverObjectivePaths(ObjectiveClass.LoadSynchronous());
		DesiredNames.RemoveAll([](const FName& N) { return N.IsNone(); });
	}
	FSimpleQuestEditorUtilities::SortPinNamesAlphabetical(DesiredNames);
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

