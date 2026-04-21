// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "K2Nodes/Slate/SGraphNode_CompleteObjective.h"
#include "K2Nodes/K2Node_CompleteObjectiveWithOutcome.h"
#include "GameplayTagContainer.h"
#include "SGameplayTagCombo.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "SGraphNode_CompleteObjective"

void SGraphNode_CompleteObjective::Construct(const FArguments& InArgs, UK2Node_CompleteObjectiveWithOutcome* InNode)
{
	SGraphNodeK2Default::Construct(SGraphNodeK2Default::FArguments(), InNode);
}

void SGraphNode_CompleteObjective::CreatePinWidgets()
{
	// Exec pins first
	for (UEdGraphPin* Pin : GraphNode->Pins)
	{
		if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
		{
			CreateStandardPinWidget(Pin);
		}
	}

	// Tag picker between exec and data pins
	UK2Node_CompleteObjectiveWithOutcome* Node = Cast<UK2Node_CompleteObjectiveWithOutcome>(GraphNode);
	if (Node && LeftNodeBox.IsValid())
	{
		LeftNodeBox->AddSlot()
			.AutoHeight()
			.Padding(FMargin(18.f, 2.f, 18.f, 6.f))
			[
				SNew(SGameplayTagCombo)
				.Filter(TEXT("Quest.Outcome"))
				.Tag_Lambda([Node]() { return Node->OutcomeTag; })
				.OnTagChanged_Lambda([this](const FGameplayTag NewTag)
				{
					OnOutcomeTagChanged(NewTag);
				})
			];
	}

	// Data pins after
	for (UEdGraphPin* Pin : GraphNode->Pins)
	{
		if (Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec)
		{
			CreateStandardPinWidget(Pin);
		}
	}
}

void SGraphNode_CompleteObjective::OnOutcomeTagChanged(const FGameplayTag NewTag)
{
	UK2Node_CompleteObjectiveWithOutcome* Node = Cast<UK2Node_CompleteObjectiveWithOutcome>(GraphNode);
	if (!Node) return;

	const FScopedTransaction Transaction(LOCTEXT("ChangeOutcome", "Change Outcome Tag"));
	Node->Modify();
	Node->OutcomeTag = NewTag;
	Node->InvalidateCachedTitle();
	UpdateGraphNode();
}

#undef LOCTEXT_NAMESPACE
