// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Nodes/QuestlineNode_Step.h"

#include "SimpleQuestLog.h"
#include "Objectives/QuestObjective.h"
#include "Quests/QuestStep.h"
#include "Utilities/SimpleQuestEditorUtils.h"

FText UQuestlineNode_Step::MakeOutcomePinLabel(const FGameplayTag& Tag)
{
	const FString Full = Tag.ToString();
	int32 LastDot;
	return Full.FindLastChar(TEXT('.'), LastDot) ? FText::FromString(Full.Mid(LastDot + 1)) : FText::FromString(Full);
}

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
				if (Pin) Pin->PinFriendlyName = MakeOutcomePinLabel(Tag);
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
	TArray<FGameplayTag> DesiredOutcomes;
	if (ObjectiveClass)
	{
		if (const UQuestObjective* CDO = GetDefault<UQuestObjective>(ObjectiveClass))
		{
			for (const FGameplayTag& Tag : CDO->GetPossibleOutcomes())
			{
				if (Tag.IsValid()) DesiredOutcomes.Add(Tag);
			}
		}
	}

	TArray<UEdGraphPin*> ExistingOutcomePins;
	for (UEdGraphPin* Pin : Pins)
	{
		if (Pin->Direction == EGPD_Output && Pin->PinType.PinCategory == TEXT("QuestOutcome")) ExistingOutcomePins.Add(Pin);
	}
	TArray<UEdGraphPin*> PinsToRemove;
	for (UEdGraphPin* Pin : ExistingOutcomePins)
	{
		const bool bStillWanted = DesiredOutcomes.ContainsByPredicate([&](const FGameplayTag& Tag){ return Pin->PinName == Tag.GetTagName(); });
		if (!bStillWanted) PinsToRemove.Add(Pin);
	}

	TArray<FGameplayTag> OutcomesToAdd;
	for (const FGameplayTag& Tag : DesiredOutcomes)
	{
		const bool bAlreadyExists = ExistingOutcomePins.ContainsByPredicate([&](const UEdGraphPin* Pin){ return Pin->PinName == Tag.GetTagName(); });
		if (!bAlreadyExists) OutcomesToAdd.Add(Tag);
	}

	if (PinsToRemove.IsEmpty() && OutcomesToAdd.IsEmpty()) return;

	Modify();

	for (UEdGraphPin* Pin : PinsToRemove)
	{
		Pin->BreakAllPinLinks(false); // suppress mid-loop NotifyGraphChanged
		RemovePin(Pin);
	}
	// Find insertion point: before any deactivation pins
	int32 InsertIndex = INDEX_NONE;
	for (int32 i = 0; i < Pins.Num(); i++)
	{
		const FName Cat = Pins[i]->PinType.PinCategory;
		if (Cat == TEXT("QuestDeactivate") || Cat == TEXT("QuestDeactivated"))
		{
			InsertIndex = i;
			break;
		}
	}
	
	FEdGraphPinType OutcomeType;
	OutcomeType.PinCategory = TEXT("QuestOutcome");

	for (const FGameplayTag& Tag : OutcomesToAdd)
	{
		CreatePin(EGPD_Output, OutcomeType, Tag.GetTagName(), InsertIndex);
		if (InsertIndex != INDEX_NONE) ++InsertIndex;
	}

	if (UEdGraph* Graph = GetGraph())
	{
		Graph->NotifyGraphChanged();
	}
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

FText UQuestlineNode_Step::GetPinDisplayName(const UEdGraphPin* Pin) const
{
	if (Pin->Direction == EGPD_Output && Pin->PinType.PinCategory == TEXT("QuestOutcome"))
	{
		const FString Full = Pin->PinName.ToString();
		int32 LastDot;
		if (Full.FindLastChar(TEXT('.'), LastDot)) return FText::FromString(Full.Mid(LastDot + 1));
	}
	return Super::GetPinDisplayName(Pin);
}
