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

		// Default directions — may be overridden below for reversed knots
		Params.StartDirection = EGPD_Output;
		Params.EndDirection = EGPD_Input;
		Params.WireThickness = this->Settings->DefaultDataWireThickness;
		this->ApplyQuestlineWireParams(OutputPin, InputPin, Params);

		// ── Reversed knot tangent detection ──
		// Mirror FKismetConnectionDrawingPolicy::ShouldChangeTangentForKnot
		// for our custom UQuestlineNode_Knot (which doesn't inherit UK2Node_Knot).
		if (OutputPin)
		{
			if (UQuestlineNode_Knot* OutputKnot = Cast<UQuestlineNode_Knot>(OutputPin->GetOwningNode()))
			{
				if (ShouldReverseKnotTangent(OutputKnot))
				{
					Params.StartDirection = EGPD_Input;
				}
			}
		}
		if (InputPin)
		{
			if (UQuestlineNode_Knot* InputKnot = Cast<UQuestlineNode_Knot>(InputPin->GetOwningNode()))
			{
				if (ShouldReverseKnotTangent(InputKnot))
				{
					Params.EndDirection = EGPD_Output;
				}
			}
		}

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
	
	bool ShouldReverseKnotTangent(const UQuestlineNode_Knot* Knot) const
	{
		// Cache lookup
		if (const bool* pCached = KnotReversalCache.Find(Knot))
			return *pCached;

		bool bReversed = false;

		FVector2f CenterPos(0.f);
		FVector2f AvgInputPos(0.f);
		FVector2f AvgOutputPos(0.f);
		bool bCenterValid = false, bInputValid = false, bOutputValid = false;

		// Get knot center from its output pin widget position
		if (const UEdGraphPin* OutPin = Knot->FindPin(TEXT("KnotOut"), EGPD_Output))
		{
			bCenterValid = this->FindPinCenter(const_cast<UEdGraphPin*>(OutPin), CenterPos);
		}

		// Average position of everything connected to input side
		if (const UEdGraphPin* InPin = Knot->FindPin(TEXT("KnotIn"), EGPD_Input))
		{
			bInputValid = GetAverageLinkedPosition(InPin, AvgInputPos);
		}

		// Average position of everything connected to output side
		if (const UEdGraphPin* OutPin = Knot->FindPin(TEXT("KnotOut"), EGPD_Output))
		{
			bOutputValid = GetAverageLinkedPosition(OutPin, AvgOutputPos);
		}

		if (bInputValid && bOutputValid)
		{
			bReversed = AvgOutputPos.X < AvgInputPos.X;
		}
		else if (bCenterValid)
		{
			if (bInputValid)  bReversed = CenterPos.X < AvgInputPos.X;
			if (bOutputValid) bReversed = AvgOutputPos.X < CenterPos.X;
		}

		KnotReversalCache.Add(Knot, bReversed);
		return bReversed;
	}

	bool GetAverageLinkedPosition(const UEdGraphPin* Pin, FVector2f& OutAvg) const
	{
		FVector2f Sum = FVector2f::ZeroVector;
		int32 Count = 0;
		for (const UEdGraphPin* Linked : Pin->LinkedTo)
		{
			FVector2f Pos;
			if (this->FindPinCenter(const_cast<UEdGraphPin*>(Linked), Pos))
			{
				Sum += Pos;
				Count++;
			}
		}
		if (Count > 0)
		{
			OutAvg = Sum / static_cast<float>(Count);
			return true;
		}
		return false;
	}

private:
	
	mutable TMap<const UQuestlineNode_Knot*, bool> KnotReversalCache;
};
