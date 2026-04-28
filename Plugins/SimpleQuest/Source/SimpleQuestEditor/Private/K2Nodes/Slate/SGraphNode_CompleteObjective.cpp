// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "K2Nodes/Slate/SGraphNode_CompleteObjective.h"
#include "K2Nodes/K2Node_CompleteObjectiveWithOutcome.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "ScopedTransaction.h"
#include "Widgets/Input/SEditableTextBox.h"

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

	// Path Name text input between exec and data pins. Always visible — hint text signals the optional
	// authoring affordance for the dynamic-placement case (OutcomeTag pin wired with a runtime tag value).
	// Designers leaving it blank get static-placement behavior (PathIdentity auto-derives from OutcomeTag).
	UK2Node_CompleteObjectiveWithOutcome* Node = Cast<UK2Node_CompleteObjectiveWithOutcome>(GraphNode);
	if (Node && LeftNodeBox.IsValid())
	{
		LeftNodeBox->AddSlot()
			.AutoHeight()
			.Padding(FMargin(18.f, 2.f, 18.f, 6.f))
			[
				SNew(SEditableTextBox)
				.Text_Lambda([Node]() {
					return Node->PathName.IsNone() ? FText() : FText::FromName(Node->PathName);
				})
				.HintText(LOCTEXT("PathNameHint", "Path Name (optional)"))
				.ToolTipText(LOCTEXT("PathNameTooltip",
					"Optional friendly identifier for this completion route. If left blank, Path Identity\n"
					"auto-derives from the chosen Outcome Tag. If the Outcome Tag is provided dynamically and\n"
					"no name is set, a pin labeled \"Dynamic\" will be automatically created for this path\n"
					"on the parent Step node.\n\n"
					"Set this when the Outcome Tag pin is wired to a runtime value (dynamic placement).\n"
					"The Step's exec output pin uses Path Name as its identity, so routing remains stable\n"
					"regardless of which runtime tag flows through the wire."))
				.OnTextCommitted(this, &SGraphNode_CompleteObjective::OnPathNameCommitted)
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

void SGraphNode_CompleteObjective::OnPathNameCommitted(const FText& NewText, ETextCommit::Type CommitType)
{
	UK2Node_CompleteObjectiveWithOutcome* Node = Cast<UK2Node_CompleteObjectiveWithOutcome>(GraphNode);
	if (!Node) return;

	const FName NewPathName = NewText.IsEmpty() ? NAME_None : FName(*NewText.ToString());
	if (Node->PathName == NewPathName) return;  // no-op (e.g., focus loss without edit)

	const FScopedTransaction Transaction(LOCTEXT("ChangePathName", "Change Path Name"));
	Node->Modify();
	Node->PathName = NewPathName;

	// Clearing PathName may transition the node into dynamic-without-PathName state — ensure a DynamicIndex
	// is allocated for that case. No-op if the node is in a different state.
	Node->EnsureDynamicIndexAllocated();

	// Title derives from PathName (priority over OutcomeTag's leaf for dynamic placements). InvalidateCachedTitle
	// dirties the title cache so the next GetNodeTitle call rebuilds; NotifyGraphChanged + UpdateGraphNode push
	// the visual refresh through the editor.
	Node->InvalidateCachedTitle();
	if (UEdGraph* Graph = Node->GetGraph())
	{
		Graph->NotifyGraphChanged();
	}
	UpdateGraphNode();

	// Mark the owning BP as structurally modified — PathName affects the K2 node's ExpandNode output, so the
	// BP needs recompile + dirties so the change persists on save. Modify() above marks the K2 node's package
	// dirty but doesn't propagate the recompile signal to the BP editor; this does.
	if (UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForNode(Node))
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	}
}

#undef LOCTEXT_NAMESPACE