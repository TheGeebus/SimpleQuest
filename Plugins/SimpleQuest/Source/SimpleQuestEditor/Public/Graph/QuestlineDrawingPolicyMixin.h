// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "BlueprintConnectionDrawingPolicy.h"
#include "Graph/QuestlineGraphSchema.h"
#include "Nodes/QuestlineNode_Knot.h"
#include "Utilities/SimpleQuestEditorUtils.h"

template <typename TBase>
class TQuestlineDrawingPolicyMixin : public TBase
{
public:
	using TBase::TBase;

	// Shared helper — called by both drawing policies to avoid duplicating color/flag logic
	static void ApplyQuestlineWireParams(UEdGraphPin* OutputPin, UEdGraphPin* InputPin, FConnectionParams& Params)
	{
		if (OutputPin)
		{
			const FName Category = OutputPin->PinType.PinCategory;
			if (Category == TEXT("QuestOutcome"))													Params.WireColor = SQ_ED_WIRE_OUTCOME;
			else if (Category == TEXT("QuestDeactivated") || Category == TEXT("QuestDeactivate"))	Params.WireColor = SQ_ED_WIRE_DEACTIVATION;
			else																					Params.WireColor = SQ_ED_WIRE_ACTIVATION;
		}

		const bool bIsPrerequisiteWire =
			(OutputPin && OutputPin->PinType.PinCategory == TEXT("QuestPrerequisite")) ||
			(InputPin  && InputPin->PinType.PinCategory  == TEXT("QuestPrerequisite"));

		if (bIsPrerequisiteWire)
		{
			Params.bUserFlag1 = true;
		}
		else if (InputPin)
		{
			// An activation-type wire is drawn dashed if every path it reaches downstream terminates at a prerequisite input.
			// It carries a signal used exclusively as a condition, not as an activation or deactivation trigger.
			TSet<const UEdGraphNode*> Visited;
			if (LeadsOnlyToPrereqInputs(InputPin, Visited)) Params.bUserFlag1 = true;
		}
	}
	
	virtual void DetermineWiringStyle(UEdGraphPin* OutputPin, UEdGraphPin* InputPin, FConnectionParams& Params) override
	{
		TBase::DetermineWiringStyle(OutputPin, InputPin, Params);

		Params.StartDirection = EGPD_Output;
		Params.EndDirection = EGPD_Input;
		Params.WireThickness = this->Settings->DefaultDataWireThickness;
		this->ApplyQuestlineWireParams(OutputPin, InputPin, Params);

		if (this->HoveredPins.Num() > 0)
		{
			this->ApplyHoverDeemphasis(OutputPin, InputPin, Params.WireThickness, Params.WireColor);
		}
	}

	virtual void DrawPreviewConnector(const FGeometry& PinGeometry, const FVector2f& StartPoint, const FVector2f& EndPoint, UEdGraphPin* Pin) override
	{
		UQuestlineGraphSchema::SetActiveDragFromPin(Pin);
		TBase::DrawPreviewConnector(PinGeometry, StartPoint, EndPoint, Pin);
	}
	
	// Returns true if every downstream terminal reachable from InputPin is a QuestPrerequisite input. Used to determine whether
	// an activation-type wire should be drawn dashed (prereq-only path).
	static bool LeadsOnlyToPrereqInputs(const UEdGraphPin* InputPin, TSet<const UEdGraphNode*>& Visited)
	{
		if (!InputPin) return false;
		const UEdGraphNode* Node = InputPin->GetOwningNode();
		if (Visited.Contains(Node)) return true; // cycle guard

		if (InputPin->PinType.PinCategory == TEXT("QuestPrerequisite"))
			return true;

		if (const UQuestlineNode_Knot* Knot = Cast<UQuestlineNode_Knot>(Node))
		{
			Visited.Add(Node);
			const UEdGraphPin* KnotOut = Knot->FindPin(TEXT("KnotOut"), EGPD_Output);
			if (!KnotOut || KnotOut->LinkedTo.IsEmpty())
			{
				return false; // unconnected downstream — no confirmed prereq path, draw solid
			}
			for (const UEdGraphPin* Linked : KnotOut->LinkedTo)
			{
				if (!LeadsOnlyToPrereqInputs(Linked, Visited)) return false;
			}
			return true;
		}
		return false; // any other non-prereq input terminal
	}
};
