// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Graph/QuestlineGraphSchema.h"

#include "Nodes/QuestlineNode_Entry.h"
#include "Nodes/QuestlineNode_Knot.h"
#include "Nodes/QuestlineNode_Quest.h"
#include "ConnectionDrawingPolicy.h"


class FQuestlineConnectionDrawingPolicy : public FConnectionDrawingPolicy
{
public:
	FQuestlineConnectionDrawingPolicy(int32 InBackLayerID, int32 InFrontLayerID,
		float InZoomFactor, const FSlateRect& InClippingRect,
		FSlateWindowElementList& InDrawElements, UEdGraph* InGraph)
		: FConnectionDrawingPolicy(InBackLayerID, InFrontLayerID, InZoomFactor, InClippingRect, InDrawElements)
		, GraphObj(InGraph)
	{}

	virtual void DetermineWiringStyle(UEdGraphPin* OutputPin, UEdGraphPin* InputPin,
		FConnectionParams& Params) override
	{
		FConnectionDrawingPolicy::DetermineWiringStyle(OutputPin, InputPin, Params);
		if (OutputPin)
		{
			Params.WireColor = GraphObj->GetSchema()->GetPinTypeColor(OutputPin->PinType);
		}
		if (InputPin && InputPin->PinName == TEXT("Prerequisites"))
		{
			Params.bUserFlag1 = true;   // signals dashed draw
		}
	}

	virtual void DrawConnection(int32 LayerId, const FVector2f& Start, const FVector2f& End,
    const FConnectionParams& Params) override
	{
		const FVector2f& P0 = Start;
		const FVector2f& P1 = End;
		
		const FVector2f SplineTangent = ComputeSplineTangent(P0, P1);
		
		const FVector2f StartTangent = (Params.StartTangent.IsNearlyZero()) ? ((Params.StartDirection == EGPD_Output) ? SplineTangent : -SplineTangent) : Params.StartTangent;
		const FVector2f EndTangent = (Params.EndTangent.IsNearlyZero()) ? ((Params.EndDirection == EGPD_Input) ? SplineTangent : -SplineTangent) : Params.EndTangent;
		
		if (Settings->bTreatSplinesLikePins)
		{
			// Distance to consider as an overlap
			const float QueryDistanceTriggerThresholdSquared = FMath::Square(Settings->SplineHoverTolerance + Params.WireThickness * 0.5f);

			// Distance to pass the bounding box cull test. This is used for the bCloseToSpline output that can be used as a
			// dead zone to avoid mistakes caused by missing a double-click on a connection.
			const float QueryDistanceForCloseSquared = FMath::Square(FMath::Sqrt(QueryDistanceTriggerThresholdSquared) + Settings->SplineCloseTolerance);

			bool bCloseToSpline = false;
			{
				// The curve will include the endpoints but can extend out of a tight bounds because of the tangents
				// P0Tangent coefficient maximizes to 4/27 at a=1/3, and P1Tangent minimizes to -4/27 at a=2/3.
				const float MaximumTangentContribution = 4.0f / 27.0f;
				FBox2f Bounds(ForceInit);

				Bounds += FVector2f(P0);
				Bounds += FVector2f(P0 + MaximumTangentContribution * StartTangent);
				Bounds += FVector2f(P1);
				Bounds += FVector2f(P1 - MaximumTangentContribution * EndTangent);

				bCloseToSpline = Bounds.ComputeSquaredDistanceToPoint(LocalMousePosition) < QueryDistanceForCloseSquared;

				// Draw the bounding box for debugging
	#if 0
	#define DrawSpaceLine(Point1, Point2, DebugWireColor) {const FVector2f FakeTangent = (Point2 - Point1).GetSafeNormal(); FSlateDrawElement::MakeDrawSpaceSpline(DrawElementsList, LayerId, Point1, FakeTangent, Point2, FakeTangent, ClippingRect, 1.0f, ESlateDrawEffect::None, DebugWireColor); }

				if (bCloseToSpline)
				{
					const FLinearColor BoundsWireColor = bCloseToSpline ? FLinearColor::Green : FLinearColor::White;

					FVector2f TL = Bounds.Min;
					FVector2f BR = Bounds.Max;
					FVector2f TR = FVector2f(Bounds.Max.X, Bounds.Min.Y);
					FVector2f BL = FVector2f(Bounds.Min.X, Bounds.Max.Y);

					DrawSpaceLine(TL, TR, BoundsWireColor);
					DrawSpaceLine(TR, BR, BoundsWireColor);
					DrawSpaceLine(BR, BL, BoundsWireColor);
					DrawSpaceLine(BL, TL, BoundsWireColor);
				}
	#endif
			}

			if (bCloseToSpline)
			{
				// Find the closest approach to the spline
				FVector2f ClosestPoint(ForceInit);
				float ClosestDistanceSquared = FLT_MAX;

				const int32 NumStepsToTest = 16;
				const float StepInterval = 1.0f / (float)NumStepsToTest;
				FVector2f Point1 = FMath::CubicInterp(P0, StartTangent, P1, EndTangent, 0.0f);
				for (float TestAlpha = 0.0f; TestAlpha < 1.0f; TestAlpha += StepInterval)
				{
					const FVector2f Point2 = FMath::CubicInterp(P0, StartTangent, P1, EndTangent, TestAlpha + StepInterval);

					const FVector2f ClosestPointToSegment = FMath::ClosestPointOnSegment2D(LocalMousePosition, Point1, Point2);
					const float DistanceSquared = (LocalMousePosition - ClosestPointToSegment).SizeSquared();

					if (DistanceSquared < ClosestDistanceSquared)
					{
						ClosestDistanceSquared = DistanceSquared;
						ClosestPoint = ClosestPointToSegment;
					}

					Point1 = Point2;
				}

				// Record the overlap
				if (ClosestDistanceSquared < QueryDistanceTriggerThresholdSquared)
				{
					if (ClosestDistanceSquared < SplineOverlapResult.GetDistanceSquared())
					{
						const float SquaredDistToPin1 = (Params.AssociatedPin1 != nullptr) ? (P0 - ClosestPoint).SizeSquared() : FLT_MAX;
						const float SquaredDistToPin2 = (Params.AssociatedPin2 != nullptr) ? (P1 - ClosestPoint).SizeSquared() : FLT_MAX;

						SplineOverlapResult = FGraphSplineOverlapResult(Params.AssociatedPin1, Params.AssociatedPin2, ClosestDistanceSquared, SquaredDistToPin1, SquaredDistToPin2, true);
					}
				}
				else if (ClosestDistanceSquared < QueryDistanceForCloseSquared)
				{
					SplineOverlapResult.SetCloseToSpline(true);
				}
			}
		}
		
		if (!Params.bUserFlag1)
		{
			FSlateDrawElement::MakeDrawSpaceSpline(
				DrawElementsList, LayerId,
				Start, StartTangent,
				End, EndTangent,
				Params.WireThickness,
				ESlateDrawEffect::None,
				Params.WireColor);
			return;
		}
		
	    const int32 NumSamples = 64;
	    TArray<FVector2f> CurvePoints;
	    CurvePoints.Reserve(NumSamples + 1);
	    for (int32 i = 0; i <= NumSamples; ++i)
	    {
	        const float T = (float)i / (float)NumSamples;
	        CurvePoints.Add(FMath::CubicInterp(Start, StartTangent, End, EndTangent, T));
	    }

	    float Accumulated = 0.f;
		bool bDrawing = true;
		TArray<FVector2f> CurrentDash;
		CurrentDash.Add(CurvePoints[0]);

		for (int32 i = 1; i < CurvePoints.Num(); ++i)
		{
			const FVector2f& A = CurvePoints[i - 1];
			const FVector2f& B = CurvePoints[i];
			const float SegLength = FVector2f::Distance(A, B);
			if (SegLength < KINDA_SMALL_NUMBER) continue;
			const FVector2f SegDir = (B - A) / SegLength;

			float Consumed = 0.f;
			while (Consumed < SegLength)
			{
				constexpr float GapLength  =  5.f;
				constexpr float DashLength = 10.f;
				const float Threshold  = bDrawing ? DashLength : GapLength;
				const float ToThreshold = Threshold - Accumulated;
				const float Available   = SegLength - Consumed;

				if (Available < ToThreshold)
				{
					// Segment ends before the next transition
					if (bDrawing) CurrentDash.Add(B);
					Accumulated += Available;
					break;
				}

				// Transition point lands inside this segment
				const FVector2f TransitionPoint = A + SegDir * (Consumed + ToThreshold);

				if (bDrawing)
				{
					CurrentDash.Add(TransitionPoint);
					if (CurrentDash.Num() >= 2)
					{
						FSlateDrawElement::MakeLines(DrawElementsList, LayerId,
							FPaintGeometry(), CurrentDash, ESlateDrawEffect::None,
							Params.WireColor, true, Params.WireThickness);
					}
					CurrentDash.Reset();
				}

				Consumed    += ToThreshold;
				Accumulated  = 0.f;
				bDrawing     = !bDrawing;

				if (bDrawing) CurrentDash.Add(A + SegDir * Consumed);
			}
		}

		// Flush any remaining dash
		if (bDrawing && CurrentDash.Num() >= 2)
		{
			FSlateDrawElement::MakeLines(DrawElementsList, LayerId,
				FPaintGeometry(), CurrentDash, ESlateDrawEffect::None,
				Params.WireColor, true, Params.WireThickness);
		}
	}

private:
	UEdGraph* GraphObj;
};

void UQuestlineGraphSchema::CreateDefaultNodesForGraph(UEdGraph& Graph) const
{
	FGraphNodeCreator<UQuestlineNode_Entry> Creator(Graph);
	UQuestlineNode_Entry* EntryNode = Creator.CreateNode();
	EntryNode->NodePosX = 0;
	EntryNode->NodePosY = 0;
	Creator.Finalize();
	SetNodeMetaData(EntryNode, FNodeMetadata::DefaultGraphNode);
}

void UQuestlineGraphSchema::CollectSourceQuests(const UEdGraphPin* Pin, TSet<UQuestlineNode_Quest*>& OutSources,
	TSet<const UEdGraphNode*>& Visited)
{
	if (!Pin) return;

	const UEdGraphNode* Node = Pin->GetOwningNode();
	if (Visited.Contains(Node)) return;
	Visited.Add(Node);

	if (UQuestlineNode_Quest* Quest = const_cast<UQuestlineNode_Quest*>(Cast<const UQuestlineNode_Quest>(Node)))
	{
		OutSources.Add(Quest);
		return;
	}

	if (const UQuestlineNode_Knot* Knot = Cast<const UQuestlineNode_Knot>(Node))
	{
		const UEdGraphPin* KnotIn = Knot->FindPin(TEXT("KnotIn"));
		if (KnotIn)
		{
			for (const UEdGraphPin* Linked : KnotIn->LinkedTo)
				CollectSourceQuests(Linked, OutSources, Visited);
		}
	}
}

const FPinConnectionResponse UQuestlineGraphSchema::CanCreateConnection(const UEdGraphPin* A, const UEdGraphPin* B) const
{
	if (!A || !B)
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, NSLOCTEXT("SimpleQuestEditor", "PinNull", "Invalid pin"));
	}

	if (A->Direction == B->Direction)
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, NSLOCTEXT("SimpleQuestEditor", "SameDirection", "Cannot connect two inputs or two outputs"));
	}

	const UEdGraphPin* OutputPin = A->Direction == EGPD_Output ? A : B;
	const UEdGraphPin* InputPin  = A->Direction == EGPD_Input  ? A : B;

	const UEdGraphNode* OutputNode = OutputPin->GetOwningNode();
	const UEdGraphNode* InputNode  = InputPin->GetOwningNode();

	const bool bOutputIsKnot = Cast<const UQuestlineNode_Knot>(OutputNode) != nullptr;
	const bool bInputIsKnot  = Cast<const UQuestlineNode_Knot>(InputNode)  != nullptr;

	if (bOutputIsKnot || bInputIsKnot)
	{
		if (bOutputIsKnot && bInputIsKnot)
		{
			if (OutputNode == InputNode)
			{
				return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW,
					NSLOCTEXT("SimpleQuestEditor", "KnotSelfLoop", "Cannot connect a reroute node to itself"));
			}

			const FName OutCat = Cast<const UQuestlineNode_Knot>(OutputNode)->GetEffectiveCategory();
			const FName InCat  = Cast<const UQuestlineNode_Knot>(InputNode)->GetEffectiveCategory();

			if (OutCat != TEXT("QuestActivation") &&
				InCat  != TEXT("QuestActivation") &&
				OutCat != InCat)
			{
				return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW,
					NSLOCTEXT("SimpleQuestEditor", "KnotTypeMismatch", "Reroute node signal types do not match"));
			}
			return FPinConnectionResponse(CONNECT_RESPONSE_MAKE, FText::GetEmpty());
		}

		if (bInputIsKnot)
		{
			const FName KnotCategory = Cast<const UQuestlineNode_Knot>(InputNode)->GetEffectiveCategory();
			const FName WireCategory = OutputPin->PinType.PinCategory;

			if (KnotCategory != TEXT("QuestActivation") && WireCategory != KnotCategory)
			{
				return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW,
					NSLOCTEXT("SimpleQuestEditor", "KnotTypeMismatch", "Signal type does not match this reroute node"));
			}
			return FPinConnectionResponse(CONNECT_RESPONSE_MAKE, FText::GetEmpty());
		}

		// bOutputIsKnot, non-knot destination — fall through to duplicate check
	}
	else
	{
		if (OutputNode == InputNode)
		{
			if (Cast<const UQuestlineNode_Quest>(OutputNode) && InputPin->PinName == TEXT("Activate"))
			{
				return FPinConnectionResponse(CONNECT_RESPONSE_MAKE, FText::GetEmpty());
			}
			return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW,
				NSLOCTEXT("SimpleQuestEditor", "SameNode", "Cannot connect a node to itself"));
		}

		if (Cast<const UQuestlineNode_Entry>(OutputNode))
		{
			if (!Cast<const UQuestlineNode_Quest>(InputNode))
			{
				return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW,
					NSLOCTEXT("SimpleQuestEditor", "EntryOnlyToQuest", "Quest Start may only connect to a Quest node"));
			}
			if (InputPin->PinName != TEXT("Activate"))
			{
				return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW,
					NSLOCTEXT("SimpleQuestEditor", "EntryOnlyActivate", "Quest Start may only connect to the Activate pin"));
			}
		}

		if (Cast<const UQuestlineNode_Quest>(OutputNode))
		{
			const bool bValidInput = Cast<const UQuestlineNode_Quest>(InputNode)
								  || Cast<const UQuestlineNode_Knot>(InputNode)
								  || Cast<const UQuestlineNode_Exit_Success>(InputNode)
								  || Cast<const UQuestlineNode_Exit_Failure>(InputNode);
			if (!bValidInput)
			{
				return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW,
					NSLOCTEXT("SimpleQuestEditor", "QuestOutputOnlyToQuest",
						"Quest outputs may only connect to other Quest nodes or exit nodes"));
			}
		}
	}

	if (!bInputIsKnot)
	{
		TSet<UQuestlineNode_Quest*> IncomingSources;
		{
			TSet<const UEdGraphNode*> Visited;
			CollectSourceQuests(OutputPin, IncomingSources, Visited);
		}
		if (IncomingSources.Num() > 0)
		{
			for (const UEdGraphPin* Existing : InputPin->LinkedTo)
			{
				TSet<UQuestlineNode_Quest*> ExistingSources;
				TSet<const UEdGraphNode*> Visited;
				CollectSourceQuests(Existing, ExistingSources, Visited);

				if (IncomingSources.Intersect(ExistingSources).Num() > 0)
				{
					return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW,
						NSLOCTEXT("SimpleQuestEditor", "DuplicateSource",
							"This input already receives a signal from that Quest node"));
				}
			}
		}
	}

	return FPinConnectionResponse(CONNECT_RESPONSE_MAKE, FText::GetEmpty());
}



void UQuestlineGraphSchema::GetGraphContextActions(FGraphContextMenuBuilder& ContextMenuBuilder) const
{
	const FText AddQuestLabel = NSLOCTEXT("SimpleQuestEditor", "AddQuestNode", "Add Quest");
	const FText AddQuestTooltip = NSLOCTEXT("SimpleQuestEditor", "AddQuestNodeTooltip", "Add a Quest node to the graph");

	// Add Quest node action
	TSharedPtr<FEdGraphSchemaAction_NewNode> NewQuestAction(
		new FEdGraphSchemaAction_NewNode(
			FText::GetEmpty(),
			AddQuestLabel,
			AddQuestTooltip,
			0));

	NewQuestAction->NodeTemplate = NewObject<UQuestlineNode_Quest>(
		const_cast<UEdGraph*>(ContextMenuBuilder.CurrentGraph));

	ContextMenuBuilder.AddAction(NewQuestAction);

	// Add Reroute node action
	TSharedPtr<FEdGraphSchemaAction_NewNode> RerouteAction(
		new FEdGraphSchemaAction_NewNode(
			FText::GetEmpty(),
			NSLOCTEXT("SimpleQuestEditor", "AddRerouteNode", "Add Reroute Node"),
			NSLOCTEXT("SimpleQuestEditor", "AddRerouteNodeTooltip", "Add a reroute node"),
			0));

	RerouteAction->NodeTemplate = NewObject<UQuestlineNode_Knot>(
		const_cast<UEdGraph*>(ContextMenuBuilder.CurrentGraph));

	ContextMenuBuilder.AddAction(RerouteAction);

	// Add Questline Success exit
	{
		TSharedPtr<FEdGraphSchemaAction_NewNode> Action(new FEdGraphSchemaAction_NewNode(
			FText::GetEmpty(),
			NSLOCTEXT("SimpleQuestEditor", "AddExitSuccess", "Add Questline Success"),
			NSLOCTEXT("SimpleQuestEditor", "AddExitSuccessTooltip", "Add a Questline Success exit node"),
			0));
		Action->NodeTemplate = NewObject<UQuestlineNode_Exit_Success>(
			const_cast<UEdGraph*>(ContextMenuBuilder.CurrentGraph));
		ContextMenuBuilder.AddAction(Action);
	}

	// Add Questline Failure exit
	{
		TSharedPtr<FEdGraphSchemaAction_NewNode> Action(new FEdGraphSchemaAction_NewNode(
			FText::GetEmpty(),
			NSLOCTEXT("SimpleQuestEditor", "AddExitFailure", "Add Questline Failure"),
			NSLOCTEXT("SimpleQuestEditor", "AddExitFailureTooltip", "Add a Questline Failed exit node"),
			0));
		Action->NodeTemplate = NewObject<UQuestlineNode_Exit_Failure>(
			const_cast<UEdGraph*>(ContextMenuBuilder.CurrentGraph));
		ContextMenuBuilder.AddAction(Action);
	}

}

void UQuestlineGraphSchema::GetContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const
{
	// Node-specific context menu actions go here
}

FLinearColor UQuestlineGraphSchema::GetPinTypeColor(const FEdGraphPinType& PinType) const
{
	if (PinType.PinCategory == TEXT("QuestSuccess"))
	{
		return FLinearColor(0.05f, 0.75f, 0.15f);   // green
	}
	if (PinType.PinCategory == TEXT("QuestFailure"))
	{
		return FLinearColor(0.85f, 0.08f, 0.08f);   // red
	}
	return FLinearColor::White;   // QuestActivation — Activate, Prerequisites, AnyOutcome, Start, knots
}

/* Automatic reroute node placement when looping back to the same node -- currently always feeds wire into left side of node, needs a fix to create a clean loop */

/*
bool UQuestlineGraphSchema::TryCreateConnection(UEdGraphPin* A, UEdGraphPin* B) const
{
	UEdGraphPin* OutputPin = A->Direction == EGPD_Output ? A : B;
	UEdGraphPin* InputPin  = A->Direction == EGPD_Input  ? A : B;

	// Intercept self-loop: insert two knots to route the wire around the node
	if (OutputPin->GetOwningNode() == InputPin->GetOwningNode() &&
		Cast<UQuestlineNode_Quest>(OutputPin->GetOwningNode()) &&
		InputPin->PinName == TEXT("Activate"))
	{
		UEdGraphNode* QuestNode = OutputPin->GetOwningNode();
		UEdGraph* Graph = QuestNode->GetGraph();
		Graph->Modify();

		const float NodeWidth  = 200.f;
		const float KnotOffset =  60.f;

		auto MakeKnot = [&](float X, float Y) -> UQuestlineNode_Knot*
		{
			FGraphNodeCreator<UQuestlineNode_Knot> Creator(*Graph);
			UQuestlineNode_Knot* Knot = Creator.CreateNode();
			Knot->NodePosX = X;
			Knot->NodePosY = Y;
			Creator.Finalize();
			if (UEdGraphPin* In  = Knot->FindPin(TEXT("KnotIn")))  In->PinType  = OutputPin->PinType;
			if (UEdGraphPin* Out = Knot->FindPin(TEXT("KnotOut"))) Out->PinType = OutputPin->PinType;
			return Knot;
		};

		UQuestlineNode_Knot* KnotRight = MakeKnot(QuestNode->NodePosX + NodeWidth, QuestNode->NodePosY - KnotOffset);
		UQuestlineNode_Knot* KnotLeft  = MakeKnot(QuestNode->NodePosX,             QuestNode->NodePosY - KnotOffset);

		Super::TryCreateConnection(OutputPin,                          KnotRight->FindPin(TEXT("KnotIn")));
		Super::TryCreateConnection(KnotRight->FindPin(TEXT("KnotOut")), KnotLeft->FindPin(TEXT("KnotIn")));
		Super::TryCreateConnection(KnotLeft->FindPin(TEXT("KnotOut")),  InputPin);

		return true;
	}

	return Super::TryCreateConnection(A, B);
}
*/

FConnectionDrawingPolicy* UQuestlineGraphSchema::CreateConnectionDrawingPolicy(
	int32 InBackLayerID, int32 InFrontLayerID, float InZoomFactor,
	const FSlateRect& InClippingRect, FSlateWindowElementList& InDrawElements,
	UEdGraph* InGraphObj) const
{
	return new FQuestlineConnectionDrawingPolicy(
		InBackLayerID, InFrontLayerID, InZoomFactor,
		InClippingRect, InDrawElements, InGraphObj);
}

