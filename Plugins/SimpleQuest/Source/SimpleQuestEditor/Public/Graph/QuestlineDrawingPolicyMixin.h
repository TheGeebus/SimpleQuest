// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "BlueprintConnectionDrawingPolicy.h"
#include "Graph/QuestlineGraphSchema.h"
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
			if (Category == TEXT("QuestOutcome")) Params.WireColor = SQ_ED_OUTCOME;
			else if (Category == TEXT("QuestAbandon")) Params.WireColor = SQ_ED_ABANDON;
			else Params.WireColor = FLinearColor::White;
		}
		const bool bIsPrerequisiteWire =
			(OutputPin && OutputPin->PinType.PinCategory == TEXT("QuestPrerequisite")) ||
			(InputPin  && InputPin->PinType.PinCategory  == TEXT("QuestPrerequisite"));
		if (bIsPrerequisiteWire) Params.bUserFlag1 = true;
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

};
