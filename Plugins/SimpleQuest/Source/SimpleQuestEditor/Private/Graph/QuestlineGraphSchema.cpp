// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Graph/QuestlineGraphSchema.h"
#include "Graph/QuestlineDrawingPolicyMixin.h"
#include "BlueprintConnectionDrawingPolicy.h"
#include "Nodes/QuestlineNode_Entry.h"
#include "Nodes/QuestlineNode_Knot.h"
#include "Nodes/QuestlineNode_Quest.h"
#include "Nodes/QuestlineNode_Exit.h"
#include "Nodes/QuestlineNode_Step.h"
#include "Nodes/QuestlineNode_LinkedQuestline.h"
#include "Nodes/Prerequisites/QuestlineNode_PrerequisiteAnd.h"
#include "Nodes/Prerequisites/QuestlineNode_PrerequisiteOr.h"
#include "Nodes/Prerequisites/QuestlineNode_PrerequisiteNot.h"
#include "Nodes/Groups/QuestlineNode_ActivationGroupExit.h"
#include "Nodes/Groups/QuestlineNode_ActivationGroupEntry.h"
#include "Nodes/Groups/QuestlineNode_PrerequisiteRuleEntry.h"
#include "Nodes/Groups/QuestlineNode_PrerequisiteRuleExit.h"
#include "Nodes/Groups/QuestlineNode_PortalEntryBase.h"
#include "Nodes/Utility/QuestlineNode_ClearBlocked.h"
#include "Nodes/Utility/QuestlineNode_SetBlocked.h"
#include "Utilities/SimpleQuestEditorUtils.h"
#include "ConnectionDrawingPolicy.h"
#include "EdGraphUtilities.h"
#include "ScopedTransaction.h"
#include "Types/QuestPinRole.h"


/*---------------------------------------------------------------------------------------------------------*
 * Connection Drawing Policy - how to actually draw the wires (splines/dashed effect/etc.)
 *---------------------------------------------------------------------------------------------------------*/

class FQuestlineConnectionDrawingPolicy	: public TQuestlineDrawingPolicyMixin<FKismetConnectionDrawingPolicy>
{
	using Super = TQuestlineDrawingPolicyMixin<FKismetConnectionDrawingPolicy>;
public:
	FQuestlineConnectionDrawingPolicy(int32 InBackLayerID, int32 InFrontLayerID, float InZoomFactor, const FSlateRect& InClippingRect,
		FSlateWindowElementList& InDrawElements, UEdGraph* InGraph)
		: Super(InBackLayerID, InFrontLayerID, InZoomFactor, InClippingRect, InDrawElements, InGraph)
	{
		GraphObj = InGraph;
	}

	virtual void DrawConnection(int32 LayerId, const FVector2f& Start, const FVector2f& End, const FConnectionParams& Params) override
	{		
		const FVector2f SplineTangent = ComputeSplineTangent(Start, End);
		const FVector2f P0Tangent = Params.StartTangent.IsNearlyZero() 
			? ((Params.StartDirection == EGPD_Output) ? SplineTangent : -SplineTangent)
			: Params.StartTangent;
		const FVector2f P1Tangent = Params.EndTangent.IsNearlyZero() 
			? ((Params.EndDirection == EGPD_Input) ? SplineTangent : -SplineTangent)
			: Params.EndTangent;
		
		if (!Params.bUserFlag1)
		{
			FConnectionDrawingPolicy::DrawConnection(LayerId, Start, End, Params); // drawing + base hover pass

			// Unconditional second hover pass (mirrors EN's ENComputeClosestPoint approach)
			const float ToleranceSq = FMath::Square(Settings->SplineHoverTolerance + Params.WireThickness * 0.5f);
			float BestDistSq = FLT_MAX;
			FVector2f BestPoint = FVector2f::ZeroVector;
			constexpr int32 N = 16;
			for (int32 i = 1; i <= N; ++i)
			{
				const FVector2f A = FMath::CubicInterp(Start, P0Tangent, End, P1Tangent, (float)(i-1)/N);
				const FVector2f B = FMath::CubicInterp(Start, P0Tangent, End, P1Tangent, (float)i/N);
				const FVector2f Closest = FMath::ClosestPointOnSegment2D(LocalMousePosition, A, B);
				const float DistSq = (LocalMousePosition - Closest).SizeSquared();
				if (DistSq < BestDistSq) { BestDistSq = DistSq; BestPoint = Closest; }
			}
			if (BestDistSq < ToleranceSq && BestDistSq < SplineOverlapResult.GetDistanceSquared())
			{
				const float D1 = Params.AssociatedPin1 ? (Start - BestPoint).SizeSquared() : FLT_MAX;
				const float D2 = Params.AssociatedPin2 ? (End   - BestPoint).SizeSquared() : FLT_MAX;
				SplineOverlapResult = FGraphSplineOverlapResult(Params.AssociatedPin1, Params.AssociatedPin2,
					BestDistSq, D1, D2, true);
			}
			return;
		}

		constexpr int32 NumSamples = 64;
		constexpr float DashLength = 10.f;
		constexpr float GapLength = 5.f;

		TArray<FVector2f> CurvePoints;
		CurvePoints.Reserve(NumSamples + 1);
		for (int32 i = 0; i <= NumSamples; ++i)
		{
			CurvePoints.Add(FMath::CubicInterp(Start, P0Tangent, End, P1Tangent, static_cast<float>(i) / NumSamples));
		}
		float Accumulated = 0.f;
		bool bDashing = true;
		TArray<FVector2f> CurrentDash;
		CurrentDash.Add(CurvePoints[0]);

		for (int32 i = 1; i < CurvePoints.Num(); ++i)
		{
			const FVector2f& A = CurvePoints[i - 1];
			const FVector2f& B = CurvePoints[i];
			const float SegLength = FVector2f::Distance(A, B);
			if (SegLength < KINDA_SMALL_NUMBER) continue;

			const FVector2f Dir = (B - A) / SegLength;
			float Consumed = 0.f;

			while (Consumed < SegLength)
			{
				const float Threshold = bDashing ? DashLength : GapLength;
				const float ToThreshold = Threshold - Accumulated;
				const float Available = SegLength - Consumed;

				if (Available < ToThreshold)
				{
					if (bDashing) CurrentDash.Add(B);
					Accumulated += Available;
					break;
				}

				const FVector2f Transition = A + Dir * (Consumed + ToThreshold);
				if (bDashing)
				{
					CurrentDash.Add(Transition);
					if (CurrentDash.Num() >= 2)
					{
						FSlateDrawElement::MakeLines(DrawElementsList, LayerId, FPaintGeometry(), CurrentDash,
							ESlateDrawEffect::None, Params.WireColor, true, Params.WireThickness);
					}
					CurrentDash.Reset();
				}

				Consumed += ToThreshold;
				Accumulated = 0.f;
				bDashing = !bDashing;
				if (bDashing) CurrentDash.Add(A + Dir * Consumed);
			}
		}
		
		if (bDashing && CurrentDash.Num() >= 2)
		{
			FSlateDrawElement::MakeLines(DrawElementsList, LayerId, FPaintGeometry(), CurrentDash, ESlateDrawEffect::None,
				Params.WireColor, true, Params.WireThickness);
		}

		const float ToleranceSq = FMath::Square(Settings->SplineHoverTolerance + Params.WireThickness * 0.5f);

		float BestDistSq = FLT_MAX;
		FVector2f BestPoint  = FVector2f::ZeroVector;

		for (int32 i = 1; i < CurvePoints.Num(); ++i)
		{
			const FVector2f Closest = FMath::ClosestPointOnSegment2D(LocalMousePosition, CurvePoints[i - 1], CurvePoints[i]);
			const float DistSq = (LocalMousePosition - Closest).SizeSquared();
			if (DistSq < BestDistSq)
			{
				BestDistSq = DistSq;
				BestPoint = Closest;
			}
		}

		if (BestDistSq < ToleranceSq && BestDistSq < SplineOverlapResult.GetDistanceSquared())
		{
			const float DistToPin1Sq = Params.AssociatedPin1 ? (Start - BestPoint).SizeSquared() : FLT_MAX;
			const float DistToPin2Sq = Params.AssociatedPin2 ? (End - BestPoint).SizeSquared() : FLT_MAX;
			SplineOverlapResult = FGraphSplineOverlapResult(Params.AssociatedPin1, Params.AssociatedPin2,
				BestDistSq, DistToPin1Sq, DistToPin2Sq, false);
		}
	}
	
	virtual void DrawPreviewConnector(const FGeometry& PinGeometry,	const FVector2f& StartPoint, const FVector2f& EndPoint,	UEdGraphPin* Pin) override
	{
		UQuestlineGraphSchema::SetActiveDragFromPin(Pin);
		FConnectionDrawingPolicy::DrawPreviewConnector(PinGeometry, StartPoint, EndPoint, Pin);
	}
	
private:
	UEdGraph* GraphObj;
};

/** Factory for the FQuestlineConnectionDrawingPolicy */

class FQuestlineConnectionFactory : public FGraphPanelPinConnectionFactory
{
	mutable bool bInProgress = false;

public:
	virtual FConnectionDrawingPolicy* CreateConnectionPolicy(
		const UEdGraphSchema* Schema,
		int32 InBackLayerID, int32 InFrontLayerID,
		float InZoomFactor,
		const FSlateRect& InClippingRect,
		FSlateWindowElementList& InDrawElements,
		UEdGraph* InGraphObj) const override
	{
		if (!Cast<const UQuestlineGraphSchema>(Schema)) return nullptr;
		return Schema->CreateConnectionDrawingPolicy(InBackLayerID, InFrontLayerID, InZoomFactor, InClippingRect, InDrawElements, InGraphObj);
	}
};

TSharedPtr<FGraphPanelPinConnectionFactory> UQuestlineGraphSchema::MakeQuestlineConnectionFactory()
{
	return MakeShared<FQuestlineConnectionFactory>();
}

/*---------------------------------------------------------------------------------------------------------*
 * Questline Graph Schema - the rules of the graph: node types, connection rules, and menu actions
 *---------------------------------------------------------------------------------------------------------*/

UQuestlineGraphSchema::UQuestlineGraphSchema()
	: TraversalPolicy(MakeUnique<FQuestlineGraphTraversalPolicy>())
{
}

void UQuestlineGraphSchema::CreateDefaultNodesForGraph(UEdGraph& Graph) const
{
	FGraphNodeCreator<UQuestlineNode_Entry> Creator(Graph);
	UQuestlineNode_Entry* EntryNode = Creator.CreateNode();
	EntryNode->NodePosX = 0;
	EntryNode->NodePosY = 0;
	Creator.Finalize();
	SetNodeMetaData(EntryNode, FNodeMetadata::DefaultGraphNode);
}

void UQuestlineGraphSchema::OnPinConnectionDoubleCicked(UEdGraphPin* PinA, UEdGraphPin* PinB, const FVector2f& GraphPosition) const
{
	if (!PinA || !PinB) return;

	UEdGraph* Graph = PinA->GetOwningNode()->GetGraph();
	if (!Graph) return;

	const FScopedTransaction Transaction(NSLOCTEXT("SimpleQuestEditor", "CreateRerouteNode", "Create Reroute Node"));
	Graph->Modify();
	PinA->GetOwningNode()->Modify();
	PinB->GetOwningNode()->Modify();

	UEdGraphPin* OutputPin = (PinA->Direction == EGPD_Output) ? PinA : PinB;
	UEdGraphPin* InputPin  = (PinA->Direction == EGPD_Input)  ? PinA : PinB;

	FGraphNodeCreator<UQuestlineNode_Knot> Creator(*Graph);
	UQuestlineNode_Knot* KnotNode = Creator.CreateNode();
	KnotNode->NodePosX = FMath::RoundToInt(GraphPosition.X);
	KnotNode->NodePosY = FMath::RoundToInt(GraphPosition.Y);
	Creator.Finalize();

	UEdGraphPin* KnotIn  = KnotNode->FindPin(TEXT("KnotIn"));
	UEdGraphPin* KnotOut = KnotNode->FindPin(TEXT("KnotOut"));
	if (!KnotIn || !KnotOut) return;

	// Inherit the wire type so the knot locks to the correct signal colour
	KnotIn->PinType = OutputPin->PinType;
	KnotOut->PinType = OutputPin->PinType;

	// Re-route: break existing link, wire through knot
	OutputPin->BreakLinkTo(InputPin);
	OutputPin->MakeLinkTo(KnotIn);
	KnotOut->MakeLinkTo(InputPin);

	Graph->NotifyGraphChanged();

	if (UObject* Outer = Graph->GetOuter())
	{
		Outer->PostEditChange();
		Outer->MarkPackageDirty();
	}
}

/*------------------------------------------------------------------------------------------*
 * Connection rule static helper functions
 *------------------------------------------------------------------------------------------*/

void UQuestlineGraphSchema::CollectUpstreamSources(const UEdGraphPin* Pin, TSet<const UEdGraphPin*>& OutSources, TSet<const UEdGraphPin*>& Visited)
{
    if (!Pin || Visited.Contains(Pin)) return;
    Visited.Add(Pin);
    if (Cast<const UQuestlineNode_Knot>(Pin->GetOwningNode()))
    {
	    const UEdGraphPin* KnotIn = Pin->GetOwningNode()->FindPin(TEXT("KnotIn"), EGPD_Input);
    	if (KnotIn)
    	{
    		for (const UEdGraphPin* Linked : KnotIn->LinkedTo)
    		{
    			CollectUpstreamSources(Linked, OutSources, Visited);
    		}
    	}
    }
    else
    {
        OutSources.Add(Pin);
    }
}

bool UQuestlineGraphSchema::PinReachesCategory(const UEdGraphPin* OutputPin, const UEdGraphNode* TargetNode, FName Category, TSet<const UEdGraphPin*>& Visited)
{
    if (!OutputPin || Visited.Contains(OutputPin)) return false;
    Visited.Add(OutputPin);
    for (const UEdGraphPin* Linked : OutputPin->LinkedTo)
    {
        if (!Linked) continue;
        if (Linked->GetOwningNode() == TargetNode && Linked->PinType.PinCategory == Category) return true;
        if (Cast<const UQuestlineNode_Knot>(Linked->GetOwningNode()))
        {
	        const UEdGraphPin* KnotOut = Linked->GetOwningNode()->FindPin(TEXT("KnotOut"), EGPD_Output);
        	if (KnotOut && PinReachesCategory(KnotOut, TargetNode, Category, Visited)) return true;        	
        }
    }
    return false;
}

bool UQuestlineGraphSchema::AnySourceReachesCategory(const UEdGraphPin* OutputPin, const UEdGraphNode* TargetNode, FName Category)
{
	TSet<const UEdGraphPin*> Sources, BackVisited;
	CollectUpstreamSources(OutputPin, Sources, BackVisited);
	// If no upstream source was found (e.g. a fresh knot with no input connected yet), treat OutputPin itself as the effective
	// source so we still catch conflicts in its existing direct connections.
	if (Sources.IsEmpty())
		Sources.Add(OutputPin);
	for (const UEdGraphPin* Source : Sources)
	{
		TSet<const UEdGraphPin*> ForwardVisited;
		if (PinReachesCategory(Source, TargetNode, Category, ForwardVisited)) return true;
	}
	return false;
}

bool UQuestlineGraphSchema::PrereqChainReachesNode(const UEdGraphNode* StartNode, const UEdGraphNode* TargetNode, TSet<const UEdGraphNode*>& Visited)
{
    if (!StartNode || Visited.Contains(StartNode)) return false;
    Visited.Add(StartNode);

    for (const UEdGraphPin* Pin : StartNode->Pins)
    {
        if (Pin->Direction != EGPD_Output) continue;
        if (Pin->PinType.PinCategory != TEXT("QuestPrerequisite")) continue;

        for (const UEdGraphPin* Linked : Pin->LinkedTo)
        {
            const UEdGraphNode* Next = Linked->GetOwningNode();
            if (Next == TargetNode) return true;

            if (Cast<const UQuestlineNode_Knot>(Next))
            {
                // Follow knot pass-through
                if (const UEdGraphPin* KnotOut = Next->FindPin(TEXT("KnotOut"), EGPD_Output))
                {
                    for (const UEdGraphPin* KnotLinked : KnotOut->LinkedTo)
                    {
                        const UEdGraphNode* KnotNext = KnotLinked->GetOwningNode();
                        if (KnotNext == TargetNode) return true;
                        if (PrereqChainReachesNode(KnotNext, TargetNode, Visited)) return true;
                    }
                }
            }
            else if (Cast<const UQuestlineNode_PrerequisiteBase>(Next))
            {
                if (PrereqChainReachesNode(Next, TargetNode, Visited)) return true;
            }
        }
    }
    return false;
}

void UQuestlineGraphSchema::CollectPrereqLeafSources(const UEdGraphNode* Node, TSet<const UEdGraphPin*>& OutSources, TSet<const UEdGraphNode*>& Visited)
{
    if (!Node || Visited.Contains(Node)) return;
    Visited.Add(Node);

    for (const UEdGraphPin* Pin : Node->Pins)
    {
        if (Pin->Direction != EGPD_Input) continue;
        if (Pin->PinType.PinCategory != TEXT("QuestPrerequisite")) continue;

        for (const UEdGraphPin* Linked : Pin->LinkedTo)
        {
            const UEdGraphNode* Source = Linked->GetOwningNode();

            if (Cast<const UQuestlineNode_PrerequisiteBase>(Source))
            {
                CollectPrereqLeafSources(Source, OutSources, Visited);
            }
            else if (Cast<const UQuestlineNode_Knot>(Source))
            {
                // Follow knot backward
                if (const UEdGraphPin* KnotIn = Source->FindPin(TEXT("KnotIn"), EGPD_Input))
                {
                    for (const UEdGraphPin* KnotLinked : KnotIn->LinkedTo)
                    {
                        const UEdGraphNode* KnotSource = KnotLinked->GetOwningNode();
                        if (Cast<const UQuestlineNode_PrerequisiteBase>(KnotSource))
                        {
                            CollectPrereqLeafSources(KnotSource, OutSources, Visited);
                        }
                        else
                        {
                            OutSources.Add(KnotLinked);
                        }
                    }
                }
            }
            else
            {
                OutSources.Add(Linked); // Leaf: quest outcome, entry outcome, etc.
            }
        }
    }
}

FPinConnectionResponse UQuestlineGraphSchema::ValidatePrerequisiteConnection(const UEdGraphPin* OutputPin, const UEdGraphPin* InputPin, const UEdGraphNode* OutputNode, const UEdGraphNode* InputNode) const
{
	const bool bOutputIsPrereq = (OutputPin->PinType.PinCategory == TEXT("QuestPrerequisite"));
	const bool bInputIsPrereq  = (InputPin->PinType.PinCategory  == TEXT("QuestPrerequisite"));

	// A QuestPrerequisite input pin may only carry one wire
	if (bInputIsPrereq && InputPin->Direction == EGPD_Input && InputPin->LinkedTo.Num() > 0)
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, NSLOCTEXT("SimpleQuestEditor", "PrerequisiteSingleInput",
			"This prerequisite input already has a connection. Use an AND or OR node to combine conditions."));
	}

	// Prereq-to-prereq: allow unless it creates a cycle
	if (bOutputIsPrereq && bInputIsPrereq)
	{
		TSet<const UEdGraphNode*> Visited;
		if (PrereqChainReachesNode(InputNode, OutputNode, Visited))
		{
			return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW,
				NSLOCTEXT("SimpleQuestEditor", "PrereqCycle",
				"This connection would create a circular prerequisite expression"));
		}
		
		// Reject duplicate: OutputNode already feeds another input on InputNode
		for (const UEdGraphPin* Pin : InputNode->Pins)
		{
			if (Pin->Direction != EGPD_Input) continue;
			if (Pin->PinType.PinCategory != TEXT("QuestPrerequisite")) continue;
			for (const UEdGraphPin* Linked : Pin->LinkedTo)
			{
				if (Linked->GetOwningNode() == OutputNode)
				{
					return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW,
						NSLOCTEXT("SimpleQuestEditor", "PrereqDuplicateInput",
						"This node already feeds another input on this operator"));
				}
			}
		}
		
		return FPinConnectionResponse(CONNECT_RESPONSE_MAKE, FText::GetEmpty());
	}	
	
	// Prereq output to non-prereq input: only the Prerequisites pin on a content node
	if (bOutputIsPrereq)
	{
		if (UQuestlineNodeBase::GetPinRoleOf(InputPin) == EQuestPinRole::PrereqIn && TraversalPolicy->IsContentNode(InputNode))
		{
			// Check self-prerequisite: does this expression reference the target node's own outcomes?
			TSet<const UEdGraphPin*> LeafSources;
			TSet<const UEdGraphNode*> Visited;
			CollectPrereqLeafSources(OutputNode, LeafSources, Visited);
			for (const UEdGraphPin* Source : LeafSources)
			{
				if (Source->GetOwningNode() == InputNode)
				{
					return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW,
						NSLOCTEXT("SimpleQuestEditor", "SelfPrerequisite",
						"This prerequisite expression includes an outcome from this quest — a quest cannot require its own completion"));
				}
			}
			return FPinConnectionResponse(CONNECT_RESPONSE_MAKE, FText::GetEmpty());
		}
		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, NSLOCTEXT("SimpleQuestEditor", "PrereqOutputInvalid",
			"Prerequisite outputs may only connect to the Prerequisites pin or other prerequisite nodes"));
	}
	
	// Non-prereq output to prereq input: only Quest outcome pins
	const bool bIsQuestOutcome = OutputPin->PinType.PinCategory == TEXT("QuestOutcome")	|| IsAnyOutcomeSource(OutputPin);

	if (bIsQuestOutcome && (TraversalPolicy->IsContentNode(OutputNode) || Cast<const UQuestlineNode_Entry>(OutputNode)))
	{
		if (AnySourceReachesCategory(OutputPin, InputNode, TEXT("QuestDeactivate")))
		{
			return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, NSLOCTEXT("SimpleQuestEditor", "PrereqDeactivateConflict",
				"An upstream source of this wire already deactivates this node — a signal cannot be both a prerequisite and a deactivation trigger"));
		}

		// Sibling dedupe: if this is a prereq-combinator input (AND/OR/NOT) or a prereq group setter condition input,
		// reject if any sibling input on the same node already carries this outcome (direct, via utility, or via group chain).
		const bool bIsCombinator   = Cast<const UQuestlineNode_PrerequisiteBase>(InputNode) != nullptr;
		const bool bIsGroupSetter  = Cast<const UQuestlineNode_PrerequisiteRuleEntry>(InputNode) != nullptr;
		if (bIsCombinator || bIsGroupSetter)
		{
			TSet<const UEdGraphPin*> IncomingSources;
			{
				TSet<const UEdGraphNode*> Visited;
				TraversalPolicy->CollectEffectiveSources(OutputPin, IncomingSources, Visited);
			}
			if (IncomingSources.Num() > 0)
			{
				for (const UEdGraphPin* SiblingPin : InputNode->Pins)
				{
					if (SiblingPin == InputPin) continue;
					if (SiblingPin->Direction != EGPD_Input) continue;
					if (SiblingPin->PinType.PinCategory != TEXT("QuestPrerequisite")) continue;
					for (const UEdGraphPin* Existing : SiblingPin->LinkedTo)
					{
						TSet<const UEdGraphPin*> SiblingSources;
						TSet<const UEdGraphNode*> Visited;
						TraversalPolicy->CollectEffectiveSources(Existing, SiblingSources, Visited);

						const UEdGraphPin* CollisionA = nullptr;
						const UEdGraphPin* CollisionB = nullptr;
						if (SignalSetsCollide(IncomingSources, SiblingSources, CollisionA, CollisionB))
						{
							return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW,
								NSLOCTEXT("SimpleQuestEditor", "PrereqSiblingDuplicateSource",
								"That outcome already feeds another condition input on this node (direct, via a utility node, or via a group)."));
						}
					}
				}
			}
		}
		return FPinConnectionResponse(CONNECT_RESPONSE_MAKE, FText::GetEmpty());
	}
	return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, NSLOCTEXT("SimpleQuestEditor", "PrereqInputInvalid",
			"Prerequisite inputs may only accept connections from Quest outcome pins"));
}

FPinConnectionResponse UQuestlineGraphSchema::ValidateDeactivationConnection(const UEdGraphPin* OutputPin, const UEdGraphPin* InputPin,	const UEdGraphNode* OutputNode, const UEdGraphNode* InputNode) const
{
	const bool bInputIsDeactivate = (InputPin->PinType.PinCategory == TEXT("QuestDeactivate"));

	if (bInputIsDeactivate)
	{
	    if (OutputPin->PinType.PinCategory == TEXT("QuestPrerequisite"))
	    {
	        return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, NSLOCTEXT("SimpleQuestEditor", "PrereqToDeactivate",
	               "Prerequisite wires may not connect to a Deactivate pin"));
	    }
	    if (!TraversalPolicy->IsContentNode(InputNode))
	    {
	        return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, NSLOCTEXT("SimpleQuestEditor", "DeactivateOnlyOnContent",
	               "The Deactivate pin is only available on Quest and Step nodes"));
	    }
    	if (AnySourceReachesCategory(OutputPin, InputNode, TEXT("QuestActivation")))
    	{
    		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, NSLOCTEXT("SimpleQuestEditor", "ActivateDeactivateConflict",
    			"An upstream source of this wire already activates this node — the same signal cannot both activate and deactivate it"));
    	}
    	if (AnySourceReachesCategory(OutputPin, InputNode, TEXT("QuestPrerequisite")))
    	{
    		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, NSLOCTEXT("SimpleQuestEditor", "PrereqDeactivateConflict",
    			"An upstream source of this wire is already a prerequisite for this node — a signal cannot be both a prerequisite and a deactivation trigger"));
    	}
	    return FPinConnectionResponse(CONNECT_RESPONSE_MAKE, FText::GetEmpty());
	}

	// QuestDeactivated output: may connect to Activate or Deactivate inputs, not Entry or Exit.
	if (TraversalPolicy->IsExitNode(InputNode) || Cast<const UQuestlineNode_Entry>(InputNode))
	{
	    return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, NSLOCTEXT("SimpleQuestEditor", "DeactivatedNotToEntryExit",
	           "Deactivated may not connect to Start or Outcome nodes"));
	}
	const FName InputCat = InputPin->PinType.PinCategory;
	if (InputCat == TEXT("QuestActivation"))
	{
		if (AnySourceReachesCategory(OutputPin, InputNode, TEXT("QuestDeactivate")))
		{
			return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, NSLOCTEXT("SimpleQuestEditor", "ActivateDeactivateConflict",
				   "This signal already deactivates this node — the same signal cannot both activate and deactivate it"));
		}
		return FPinConnectionResponse(CONNECT_RESPONSE_MAKE, FText::GetEmpty());
	}
	if (InputCat == TEXT("QuestDeactivate"))
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_MAKE, FText::GetEmpty());
	}
    return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, NSLOCTEXT("SimpleQuestEditor", "DeactivatedInvalidTarget",
           "Deactivated may only connect to an Activate or Deactivate pin"));
}

FPinConnectionResponse UQuestlineGraphSchema::ValidateKnotConnection(const UEdGraphPin* OutputPin, const UEdGraphPin* InputPin,	const UEdGraphNode* OutputNode,
	const UEdGraphNode* InputNode, bool bOutputIsKnot, bool bInputIsKnot) const
{
    if (bOutputIsKnot && bInputIsKnot && OutputNode == InputNode)
    {
        return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, NSLOCTEXT("SimpleQuestEditor", "KnotSelfLoop", "Cannot connect a reroute node to itself"));
    }

	// ---- Knot output → content node: enforce activate/deactivate/prereq conflict rules ----
	if (bOutputIsKnot && !bInputIsKnot && TraversalPolicy->IsContentNode(InputNode))
	{
	    const FName InputCat = InputPin->PinType.PinCategory;
		if (UQuestlineNodeBase::GetPinRoleOf(InputPin) == EQuestPinRole::ExecIn	&& InputCat == TEXT("QuestActivation"))
	    {
	        if (AnySourceReachesCategory(OutputPin, InputNode, TEXT("QuestDeactivate")))
	        {
	            return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, NSLOCTEXT("SimpleQuestEditor", "ActivateDeactivateConflict",
	                   "An upstream source of this wire already deactivates this node. The same signal cannot both activate and deactivate a node."));
	        }
	    }
	    else if (InputCat == TEXT("QuestDeactivate"))
	    {
	        if (OutputPin->PinType.PinCategory == TEXT("QuestPrerequisite"))
	        {
	            return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, NSLOCTEXT("SimpleQuestEditor", "PrereqToDeactivate",
	                   "Prerequisite wires may not connect to a Deactivate pin"));
	        }
	        if (AnySourceReachesCategory(OutputPin, InputNode, TEXT("QuestActivation")))
	        {
	            return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, NSLOCTEXT("SimpleQuestEditor", "ActivateDeactivateConflict",
	                   "An upstream source of this wire already activates this node — the same signal cannot both activate and deactivate it"));
	        }
	        if (AnySourceReachesCategory(OutputPin, InputNode, TEXT("QuestPrerequisite")))
	        {
	            return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, NSLOCTEXT("SimpleQuestEditor", "PrereqDeactivateConflict",
	                   "An upstream source of this wire is already a prerequisite for this node — a signal cannot be both a prerequisite and a deactivation trigger"));
	        }
	    }
	    else if (InputCat == TEXT("QuestPrerequisite"))
	    {
	        if (AnySourceReachesCategory(OutputPin, InputNode, TEXT("QuestDeactivate")))
	        {
	            return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, NSLOCTEXT("SimpleQuestEditor", "PrereqDeactivateConflict",
	                   "An upstream source of this wire already deactivates this node — a signal cannot be both a prerequisite and a deactivation trigger"));
	        }
	    }
	}

    // ---- Knot input already connected: category and prereq fan checks ----
    if (bInputIsKnot && InputPin->LinkedTo.Num() > 0)
    {
        const UQuestlineNode_Knot* InputKnot = Cast<const UQuestlineNode_Knot>(InputNode);
        const FName TargetCategory = InputKnot->GetEffectiveCategory();
        const FName SourceCategory = bOutputIsKnot
            ? Cast<const UQuestlineNode_Knot>(OutputNode)->GetEffectiveCategory()
            : OutputPin->PinType.PinCategory;
        if (TargetCategory != SourceCategory)
        {
            const FText Msg = bOutputIsKnot
                ? NSLOCTEXT("SimpleQuestEditor", "KnotTypeMismatch", "Reroute node signal types do not match")
                : NSLOCTEXT("SimpleQuestEditor", "KnotTypeMismatchSingle", "Signal type does not match this reroute node");
            return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, Msg);
        }

    	const FName EffectiveCategory = Cast<const UQuestlineNode_Knot>(InputNode)->GetEffectiveCategory();
    	if (EffectiveCategory == TEXT("QuestPrerequisite"))
    	{
    		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, NSLOCTEXT("SimpleQuestEditor", "PrerequisiteRerouteMultiple", "Use an AND node to combine prerequisite conditions."));
    	}
    }

    // ---- Knot input: reachability, duplicate source, and parallel path checks ----
    if (bInputIsKnot)
    {
        if (const UEdGraphPin* KnotOutPin = TraversalPolicy->GetPassThroughOutputPin(InputNode))
        {
            const FQuestlineGraphTraversalPolicy::FPinReachability From = TraversalPolicy->ComputeFullReachability(OutputPin, OutputNode);
        	const FQuestlineGraphTraversalPolicy::FPinReachability To = TraversalPolicy->ComputeForwardReachability(KnotOutPin);
        	if ((From.bReachesExit && To.bReachesContent) || (From.bReachesContent && To.bReachesExit))
        	{
        		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, NSLOCTEXT("SimpleQuestEditor", "KnotMergesMixed",
					"This connection would merge an exit path with a progression path on the same wire."));
        	}
        }
        if (const FPinConnectionResponse R = CheckDuplicateSources(OutputPin, InputPin, bOutputIsKnot); R.Response != CONNECT_RESPONSE_MAKE) return R;
        return CheckDownstreamParallelPaths(OutputPin, InputPin);
    }

    // bOutputIsKnot && !bInputIsKnot: conflict checks passed, fall through to duplicate-source check
    return CheckDuplicateSources(OutputPin, InputPin, bOutputIsKnot);
}

bool UQuestlineGraphSchema::IsAnyOutcomeSource(const UEdGraphPin* Pin)
{
	// Unconditional source sentinels — content nodes' "Any Outcome" (per-node) and Entry nodes' "Entered" (per-graph). Both
	// fire unconditionally when their owning node activates, so collision behavior and prereq-eligibility are identical. The
	// role enum unifies both names under AnyOutcomeOut — schema treats them uniformly.
	return UQuestlineNodeBase::GetPinRoleOf(Pin) == EQuestPinRole::AnyOutcomeOut;
}

bool UQuestlineGraphSchema::PinsRepresentSameSignal(const UEdGraphPin* A, const UEdGraphPin* B)
{
	if (!A || !B) return false;
	if (A == B) return true;

	const UEdGraphNode* NodeA = A->GetOwningNode();
	const UEdGraphNode* NodeB = B->GetOwningNode();
	if (NodeA != NodeB) return false;

	// Same node: AnyOutcome absorbs specifics and collides with another AnyOutcome on the same node.
	if (IsAnyOutcomeSource(A) || IsAnyOutcomeSource(B)) return true;

	// Same node, both specific outcomes: identity by pin name.
	return A->PinName == B->PinName;
}

bool UQuestlineGraphSchema::SignalSetsCollide(const TSet<const UEdGraphPin*>& SetA, const TSet<const UEdGraphPin*>& SetB, const UEdGraphPin*& OutCollisionA, const UEdGraphPin*& OutCollisionB)
{
	for (const UEdGraphPin* A : SetA)
	{
		for (const UEdGraphPin* B : SetB)
		{
			if (PinsRepresentSameSignal(A, B))
			{
				OutCollisionA = A;
				OutCollisionB = B;
				return true;
			}
		}
	}
	OutCollisionA = nullptr;
	OutCollisionB = nullptr;
	return false;
}


bool UQuestlineGraphSchema::KnotLeadsToPrereq(const UQuestlineNode_Knot* StartKnot)
{
	TArray<const UQuestlineNode_Knot*> Stack;
	TSet<const UQuestlineNode_Knot*> Visited;
	Stack.Add(StartKnot);
	while (!Stack.IsEmpty())
	{
		const UQuestlineNode_Knot* Knot = Stack.Pop();
		if (Visited.Contains(Knot)) continue;
		Visited.Add(Knot);
		const UEdGraphPin* KnotOut = Knot->FindPin(TEXT("KnotOut"), EGPD_Output);
		if (!KnotOut) continue;
		for (const UEdGraphPin* LinkedPin : KnotOut->LinkedTo)
		{
			if (LinkedPin->PinType.PinCategory == TEXT("QuestPrerequisite"))
			{
				return true;
			}
			if (const UQuestlineNode_Knot* Next = Cast<UQuestlineNode_Knot>(LinkedPin->GetOwningNode()))
			{
				Stack.Add(Next);
			}
		}
	}
	return false;
}

FPinConnectionResponse UQuestlineGraphSchema::CheckDuplicateSources(const UEdGraphPin* OutputPin, const UEdGraphPin* InputPin, bool bOutputIsKnot) const
{
	// Collect the effective source outcome pins of the proposed wire.
	TSet<const UEdGraphPin*> IncomingSources;
	{
		TSet<const UEdGraphNode*> Visited;
		TraversalPolicy->CollectEffectiveSources(OutputPin, IncomingSources, Visited);
	}
	if (IncomingSources.Num() == 0)
	{
		// Empty source set: the wire traces back to an unresolved origin. Allow the connection; the signal identity becomes
		// concrete when the upstream side is wired, and standard UE knot category inference propagates at that time.
		return FPinConnectionResponse(CONNECT_RESPONSE_MAKE, FText::GetEmpty());
	}

	// Compare against every existing wire feeding this input using the unified AnyOutcome-aware comparator.
	for (const UEdGraphPin* Existing : InputPin->LinkedTo)
	{
		TSet<const UEdGraphPin*> ExistingSources;
		TSet<const UEdGraphNode*> Visited;
		TraversalPolicy->CollectEffectiveSources(Existing, ExistingSources, Visited);

		const UEdGraphPin* CollisionA = nullptr;
		const UEdGraphPin* CollisionB = nullptr;
		if (SignalSetsCollide(IncomingSources, ExistingSources, CollisionA, CollisionB))
		{
			FText Msg;
			if (bOutputIsKnot && IncomingSources.Num() > 1)
			{
				Msg = NSLOCTEXT("SimpleQuestEditor", "DuplicateSourceKnotSingle",
					"This input already receives a signal from one of those source outcomes (direct, via a utility node, or via an activation group).");
			}
			else
			{
				Msg = NSLOCTEXT("SimpleQuestEditor", "DuplicateSource",
					"This input already receives a signal from that outcome (direct, via a utility node, or via an activation group).");
			}
			return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, Msg);
		}
	}

	return FPinConnectionResponse(CONNECT_RESPONSE_MAKE, FText::GetEmpty());
}

FPinConnectionResponse UQuestlineGraphSchema::CheckGroupSetterForwardReach(const UEdGraphPin* OutputPin, const UEdGraphPin* InputPin, const UEdGraphNode* InputNode) const
{
    const UQuestlineNode_ActivationGroupEntry* Setter = Cast<const UQuestlineNode_ActivationGroupEntry>(InputNode);
    if (!Setter) return FPinConnectionResponse(CONNECT_RESPONSE_MAKE, FText::GetEmpty());

    const FGameplayTag SetterTag = Setter->GroupTag;
    if (!SetterTag.IsValid()) return FPinConnectionResponse(CONNECT_RESPONSE_MAKE, FText::GetEmpty());

    const UEdGraph* Graph = Setter->GetGraph();
    if (!Graph) return FPinConnectionResponse(CONNECT_RESPONSE_MAKE, FText::GetEmpty());

    // Collect every destination reached by same-graph getters matching this setter's tag.
    TSet<const UEdGraphPin*> ReachedTerminals;
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        const UQuestlineNode_ActivationGroupExit* Getter = Cast<UQuestlineNode_ActivationGroupExit>(Node);
        if (!Getter || Getter->GroupTag != SetterTag) continue;

		if (const UEdGraphPin* Forward = Getter->GetPinByRole(EQuestPinRole::ExecForwardOut))
        {
            TSet<const UEdGraphNode*> Visited;
            TraversalPolicy->CollectActivationTerminals(Forward, ReachedTerminals, Visited);
        }
    }
    if (ReachedTerminals.Num() == 0) return FPinConnectionResponse(CONNECT_RESPONSE_MAKE, FText::GetEmpty());

    // Effective sources of the new wire.
    TSet<const UEdGraphPin*> NewSources;
    {
        TSet<const UEdGraphNode*> Visited;
        TraversalPolicy->CollectEffectiveSources(OutputPin, NewSources, Visited);
    }
    if (NewSources.Num() == 0) return FPinConnectionResponse(CONNECT_RESPONSE_MAKE, FText::GetEmpty());

    // At each terminal, check for collisions with existing wires.
    for (const UEdGraphPin* Terminal : ReachedTerminals)
    {
        for (const UEdGraphPin* Existing : Terminal->LinkedTo)
        {
            TSet<const UEdGraphPin*> ExistingSources;
            TSet<const UEdGraphNode*> Visited;
            TraversalPolicy->CollectEffectiveSources(Existing, ExistingSources, Visited);

            const UEdGraphPin* CollisionA = nullptr;
            const UEdGraphPin* CollisionB = nullptr;
            if (SignalSetsCollide(NewSources, ExistingSources, CollisionA, CollisionB))
            {
                return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW,
                    NSLOCTEXT("SimpleQuestEditor", "GroupSetterForwardParallel",
                    "Wiring this source into the group would create a parallel path to a destination the group already reaches through another route."));
            }
        }
    }

    return FPinConnectionResponse(CONNECT_RESPONSE_MAKE, FText::GetEmpty());
}

FPinConnectionResponse UQuestlineGraphSchema::CheckDownstreamParallelPaths(const UEdGraphPin* OutputPin, const UEdGraphPin* KnotInputPin) const
{
	// Walk backward from the proposed upstream wire to collect effective source pins.
	TSet<const UEdGraphPin*> IncomingSources;
	{
		TSet<const UEdGraphNode*> Visited;
		TraversalPolicy->CollectEffectiveSources(OutputPin, IncomingSources, Visited);
	}
	if (IncomingSources.Num() == 0)
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_MAKE, FText::GetEmpty());
	}

	// Find every downstream terminal reached through the knot about to receive this upstream wire.
	const UEdGraphNode* KnotNode = KnotInputPin->GetOwningNode();
	TArray<const UEdGraphPin*> DownstreamTerminals;
	{
		TSet<const UEdGraphNode*> V;
		V.Add(KnotNode);
		TraversalPolicy->CollectDownstreamTerminalInputs(TraversalPolicy->GetPassThroughOutputPin(KnotNode), DownstreamTerminals, V);
	}

	// For each terminal, compare the new upstream sources against every existing wire already feeding it.
	for (const UEdGraphPin* Terminal : DownstreamTerminals)
	{
		for (const UEdGraphPin* Existing : Terminal->LinkedTo)
		{
			TSet<const UEdGraphPin*> ExistingSources;
			TSet<const UEdGraphNode*> Visited;
			TraversalPolicy->CollectEffectiveSources(Existing, ExistingSources, Visited);

			const UEdGraphPin* CollisionA = nullptr;
			const UEdGraphPin* CollisionB = nullptr;
			if (SignalSetsCollide(IncomingSources, ExistingSources, CollisionA, CollisionB))
			{
				return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW,
					NSLOCTEXT("SimpleQuestEditor", "DuplicatePathViaReroute", "This would create a parallel path to a destination node via another connection."));
			}
		}
	}

	return FPinConnectionResponse(CONNECT_RESPONSE_MAKE, FText::GetEmpty());
}

const FPinConnectionResponse UQuestlineGraphSchema::CanCreateConnection(const UEdGraphPin* A, const UEdGraphPin* B) const
{
	Super::CanCreateConnection(A, B);
    if (!A || !B) return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, NSLOCTEXT("SimpleQuestEditor", "PinNull", "Invalid pin"));

    if (A->Direction == B->Direction) return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, NSLOCTEXT("SimpleQuestEditor", "SameDirection", "Cannot connect two inputs or two outputs"));

    const UEdGraphPin*  OutputPin  = (A->Direction == EGPD_Output) ? A : B;
    const UEdGraphPin*  InputPin   = (A->Direction == EGPD_Input)  ? A : B;
    const UEdGraphNode* OutputNode = OutputPin->GetOwningNode();
    const UEdGraphNode* InputNode  = InputPin->GetOwningNode();

    const bool bOutputIsKnot = TraversalPolicy->IsPassThroughNode(OutputNode);
    const bool bInputIsKnot  = TraversalPolicy->IsPassThroughNode(InputNode);

	// Self-loop: only outcome/any-outcome outputs may loop back to own Activate
	if (OutputNode == InputNode && !bOutputIsKnot && !bInputIsKnot)
	{
		const FName OutputCategory = OutputPin->PinType.PinCategory;
		if (TraversalPolicy->IsContentNode(OutputNode)
			&& UQuestlineNodeBase::GetPinRoleOf(InputPin) == EQuestPinRole::ExecIn
			&& (OutputCategory == TEXT("QuestOutcome") || OutputCategory == TEXT("QuestActivation")))
		{
			// Self-loop still subject to the same dedupe as any other Activate connection.
			return CheckDuplicateSources(OutputPin, InputPin, false);
		}
		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, NSLOCTEXT("SimpleQuestEditor", "IllegalSelfLoop",
				"Only outcome outputs may loop back to the same node's Activate pin"));
	}

	// ---- Direct prerequisite rules ----
	const bool bOutputIsPrereq = (OutputPin->PinType.PinCategory == TEXT("QuestPrerequisite"));
	const bool bInputIsPrereq  = (InputPin->PinType.PinCategory  == TEXT("QuestPrerequisite"));

	if ((bOutputIsPrereq || bInputIsPrereq) && !bOutputIsKnot && !bInputIsKnot)
	{
		return ValidatePrerequisiteConnection(OutputPin, InputPin, OutputNode, InputNode);
	}
	
	// ---- Knot → prerequisite gates ----

	// Case 1: KnotOut → Prerequisite input (not a knot-to-knot connection).
	if (bOutputIsKnot && bInputIsPrereq && !bInputIsKnot)
	{
	    if (InputPin->LinkedTo.Num() > 0)
	    {
	        return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, NSLOCTEXT("SimpleQuestEditor", "PrerequisiteSingleInput",
	            "This prerequisite input already has a connection. Use an AND or OR node to combine conditions."));
	    }
	    const UEdGraphPin* KnotIn = OutputNode->FindPin(TEXT("KnotIn"), EGPD_Input);
	    if (KnotIn && KnotIn->LinkedTo.Num() > 1)
	    {
	        return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, NSLOCTEXT("SimpleQuestEditor", "PrereqKnotMultipleSources",
	            "This reroute node carries multiple signals. Use an AND or OR node to composite them before connecting to a prerequisite."));
	    }
	    if (KnotIn && KnotIn->LinkedTo.Num() > 0)
	    {
	        const FName KnotOutCat = OutputPin->PinType.PinCategory;
	        const bool bCompatible = KnotOutCat == TEXT("QuestOutcome")
	                              || KnotOutCat == TEXT("QuestPrerequisite")
	                              || KnotOutCat == TEXT("QuestActivation");
	        if (!bCompatible)
	        {
	            return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, NSLOCTEXT("SimpleQuestEditor", "PrereqKnotBadCategory",
	                "Only outcome or prerequisite signals may feed a prerequisite input through a reroute node."));
	        }
	    }
	    return FPinConnectionResponse(CONNECT_RESPONSE_MAKE, FText::GetEmpty());
	}

	// Case 2: Non-knot signal → KnotIn, where that knot leads to a prerequisite input.
	if (bInputIsKnot && !bOutputIsKnot)
	{
	    if (const UQuestlineNode_Knot* InputKnot = Cast<UQuestlineNode_Knot>(InputNode))
	    {
	        if (KnotLeadsToPrereq(InputKnot))
	        {
	            const UEdGraphPin* KnotIn = InputNode->FindPin(TEXT("KnotIn"), EGPD_Input);
	            if (KnotIn && KnotIn->LinkedTo.Num() >= 1)
	            {
	                return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, NSLOCTEXT("SimpleQuestEditor", "PrereqKnotMultipleSources",
	                    "This reroute node already has a source and leads to a prerequisite input. Use an AND or OR node to combine conditions."));
	            }
	        	const FName OutCat = OutputPin->PinType.PinCategory;
	        	const bool bCompatible = OutCat == TEXT("QuestOutcome")
									  || OutCat == TEXT("QuestPrerequisite")
									  || IsAnyOutcomeSource(OutputPin);
	            if (!bCompatible)
	            {
	                return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, NSLOCTEXT("SimpleQuestEditor", "PrereqKnotBadCategory",
	                    "Only outcome or prerequisite signals may feed a prerequisite input through a reroute node."));
	            }
	        }
	    }
	}

	// Case 3: Knot to Knot, where the destination knot leads to a prerequisite input.
	if (bOutputIsKnot && bInputIsKnot)
	{
	    if (const UQuestlineNode_Knot* InputKnot = Cast<UQuestlineNode_Knot>(InputNode))
	    {
	        if (KnotLeadsToPrereq(InputKnot))
	        {
	            const UEdGraphPin* KnotIn = InputNode->FindPin(TEXT("KnotIn"), EGPD_Input);
	            if (KnotIn && KnotIn->LinkedTo.Num() >= 1)
	            {
	                return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, NSLOCTEXT("SimpleQuestEditor", "PrereqKnotMultipleSources",
	                    "This reroute node already has a source and leads to a prerequisite input. Use an AND or OR node to combine conditions."));
	            }
	        }
	    }
	}

	// ---- Deactivation wires ----
	const bool bOutputIsDeactivated = (OutputPin->PinType.PinCategory == TEXT("QuestDeactivated"));
	const bool bInputIsDeactivate   = (InputPin->PinType.PinCategory  == TEXT("QuestDeactivate"));

	if ((bOutputIsDeactivated || bInputIsDeactivate) && !bOutputIsKnot && !bInputIsKnot)
	{
		return ValidateDeactivationConnection(OutputPin, InputPin, OutputNode, InputNode);
	}
	
	// ---- Activate/Deactivate conflict (non-knot, non-deactivation-category) ----
	if (!bOutputIsKnot && !bInputIsKnot
		&& UQuestlineNodeBase::GetPinRoleOf(InputPin) == EQuestPinRole::ExecIn
		&& InputPin->PinType.PinCategory == TEXT("QuestActivation")
		&& TraversalPolicy->IsContentNode(InputNode))
	{
		if (AnySourceReachesCategory(OutputPin, InputNode, TEXT("QuestDeactivate")))
		{
			return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, NSLOCTEXT("SimpleQuestEditor", "ActivateDeactivateConflict",
				   "An upstream source of this wire already deactivates this node. The same signal cannot both activate and deactivate a node."));
		}
	}

	// ---- Exit node enforcement ----
    if (TraversalPolicy->IsExitNode(InputNode))
    {
    	TSet<UQuestlineNode_ContentBase*> QuestSources;
	    { TSet<const UEdGraphNode*> V; TraversalPolicy->CollectSourceContentNodes(OutputPin, QuestSources, V); }
    	for (UQuestlineNode_ContentBase* QuestSourceNode : QuestSources)
    	{
    		for (UEdGraphPin* SourcePin : QuestSourceNode->Pins)
    		{
    			if (SourcePin->Direction != EGPD_Output) continue;
    			TSet<const UEdGraphNode*> V;
    			if (TraversalPolicy->LeadsToNode(SourcePin, InputNode, V))
    			{
    				return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, NSLOCTEXT("SimpleQuestEditor", "QuestAlreadyToExit",
						"A quest node in the intended connection is already connected to this Questline end node."));
    			}
    		}
    	}
    }

    // ---- Knot (reroute) node routing ----
    if (bOutputIsKnot || bInputIsKnot)
    {
	    return ValidateKnotConnection(OutputPin, InputPin, OutputNode, InputNode, bOutputIsKnot, bInputIsKnot);
    }
	
    // ---- Entry node: only connects to Quest/Step Activate or utility node Activate ----
    if (Cast<const UQuestlineNode_Entry>(OutputNode))
    {
    	const UQuestlineNodeBase* InputBase = Cast<const UQuestlineNodeBase>(InputNode);
    	const bool bIsUtility = InputBase && InputBase->IsUtilityNode();
    	const bool bIsGroupSetter = Cast<const UQuestlineNode_PortalEntryBase>(InputNode) != nullptr;
    	if (!TraversalPolicy->IsContentNode(InputNode) && !bIsUtility && !bIsGroupSetter)
    	{
    		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, NSLOCTEXT("SimpleQuestEditor", "EntryOnlyToQuest", "Quest Start may only connect to a Quest node or utility node"));
    	}

    	// Group setter inputs accept QuestActivation too (Activate_N on activation setters, Condition_N on prereq setters),
    	// but we only allow the Activate_N case from Entry — Entry firing is an activation signal, not a prereq condition.
    	const bool bSetterActivateInput = bIsGroupSetter
			&& InputPin->PinType.PinCategory == TEXT("QuestActivation");
		if (!bSetterActivateInput && UQuestlineNodeBase::GetPinRoleOf(InputPin) != EQuestPinRole::ExecIn)
    	{
    		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, NSLOCTEXT("SimpleQuestEditor", "EntryOnlyActivate", "Quest Start may only connect to an Activate pin"));
    	}
    }

	// ---- Content node outputs ----
	if (TraversalPolicy->IsContentNode(OutputNode))
	{
		const UQuestlineNodeBase* InputBase = Cast<const UQuestlineNodeBase>(InputNode);
		const bool bIsGroupSetter = Cast<const UQuestlineNode_PortalEntryBase>(InputNode) != nullptr;
		const bool bValidInput = TraversalPolicy->IsContentNode(InputNode)
			|| TraversalPolicy->IsPassThroughNode(InputNode)
			|| TraversalPolicy->IsExitNode(InputNode)
			|| (InputBase && InputBase->IsUtilityNode())
			|| bIsGroupSetter;
		if (!bValidInput)
		{
			return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, NSLOCTEXT("SimpleQuestEditor", "QuestOutputOnlyToQuest",
					"Quest outputs may only connect to other Quest nodes, exit nodes, utility nodes, or group setter nodes"));
		}
	}
	
	// If the destination is an activation group setter, walk forward from every matching-tag getter to make sure the signal
	// we're about to pipe through the group doesn't create a parallel path to any destination the group already reaches.
	if (Cast<const UQuestlineNode_ActivationGroupEntry>(InputNode))
	{
		if (const FPinConnectionResponse R = CheckGroupSetterForwardReach(OutputPin, InputPin, InputNode);
			R.Response != CONNECT_RESPONSE_MAKE)
		{
			return R;
		}
	}

	return CheckDuplicateSources(OutputPin, InputPin, bOutputIsKnot);
}

void UQuestlineGraphSchema::BreakSinglePinLink(UEdGraphPin* SourcePin, UEdGraphPin* TargetPin) const
{
	Super::BreakSinglePinLink(SourcePin, TargetPin);
	if (SourcePin)
	{
		if (UEdGraph* Graph = SourcePin->GetOwningNode() ? SourcePin->GetOwningNode()->GetGraph() : nullptr)
		{
			Graph->NotifyGraphChanged();
		}
	}
}

void UQuestlineGraphSchema::BreakPinLinks(UEdGraphPin& TargetPin, bool bSendsNodeNotification) const
{
	Super::BreakPinLinks(TargetPin, bSendsNodeNotification);
	if (UEdGraph* Graph = TargetPin.GetOwningNode() ? TargetPin.GetOwningNode()->GetGraph() : nullptr)
	{
		Graph->NotifyGraphChanged();
	}
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

	NewQuestAction->NodeTemplate = NewObject<UQuestlineNode_Quest>(const_cast<UEdGraph*>(ContextMenuBuilder.CurrentGraph));

	ContextMenuBuilder.AddAction(NewQuestAction);

	// Add Reroute node action
	TSharedPtr<FEdGraphSchemaAction_NewNode> RerouteAction(
		new FEdGraphSchemaAction_NewNode(
			FText::GetEmpty(),
			NSLOCTEXT("SimpleQuestEditor", "AddRerouteNode", "Add Reroute Node"),
			NSLOCTEXT("SimpleQuestEditor", "AddRerouteNodeTooltip", "Add a reroute node"),
			0));

	RerouteAction->NodeTemplate = NewObject<UQuestlineNode_Knot>(const_cast<UEdGraph*>(ContextMenuBuilder.CurrentGraph));

	ContextMenuBuilder.AddAction(RerouteAction);

	// Add Questline exit
	{
		TSharedPtr<FEdGraphSchemaAction_NewNode> Action(new FEdGraphSchemaAction_NewNode(
			FText::GetEmpty(),
			NSLOCTEXT("SimpleQuestEditor", "AddExit", "Add Questline Outcome"),
			NSLOCTEXT("SimpleQuestEditor", "AddExitTooltip", "Add a Questline outcome node"),
			0));
		Action->NodeTemplate = NewObject<UQuestlineNode_Exit>(const_cast<UEdGraph*>(ContextMenuBuilder.CurrentGraph));
		ContextMenuBuilder.AddAction(Action);
	}

	// Add Leaf node action
	{
		TSharedPtr<FEdGraphSchemaAction_NewNode> Action(new FEdGraphSchemaAction_NewNode(
			FText::GetEmpty(),
			NSLOCTEXT("SimpleQuestEditor", "AddLeafNode", "Add Quest Step"),
			NSLOCTEXT("SimpleQuestEditor", "AddLeafNodeTooltip", "Add a Quest Step leaf node"),
			0));
		Action->NodeTemplate = NewObject<UQuestlineNode_Step>(const_cast<UEdGraph*>(ContextMenuBuilder.CurrentGraph));
		ContextMenuBuilder.AddAction(Action);
	}

	// Add Linked Questline node action
	{
		TSharedPtr<FEdGraphSchemaAction_NewNode> Action(new FEdGraphSchemaAction_NewNode(
			FText::GetEmpty(),
			NSLOCTEXT("SimpleQuestEditor", "AddLinkedNode", "Add Linked Questline"),
			NSLOCTEXT("SimpleQuestEditor", "AddLinkedNodeTooltip", "Reference an external questline graph asset"),
			0));
		Action->NodeTemplate = NewObject<UQuestlineNode_LinkedQuestline>(const_cast<UEdGraph*>(ContextMenuBuilder.CurrentGraph));
		ContextMenuBuilder.AddAction(Action);
	}
	// Prereq AND
	{
	    TSharedPtr<FEdGraphSchemaAction_NewNode> Action(new FEdGraphSchemaAction_NewNode(
	        NSLOCTEXT("SimpleQuestEditor", "PrereqCategory", "Prerequisite"),
	        NSLOCTEXT("SimpleQuestEditor", "AddPrereqAnd", "Add AND"),
	        NSLOCTEXT("SimpleQuestEditor", "AddPrereqAndTooltip", "All wired conditions must be satisfied"),
	        0));
	    Action->NodeTemplate = NewObject<UQuestlineNode_PrerequisiteAnd>(const_cast<UEdGraph*>(ContextMenuBuilder.CurrentGraph));
	    ContextMenuBuilder.AddAction(Action);
	}

	// Prereq OR
	{
	    TSharedPtr<FEdGraphSchemaAction_NewNode> Action(new FEdGraphSchemaAction_NewNode(
	        NSLOCTEXT("SimpleQuestEditor", "PrereqCategory", "Prerequisite"),
	        NSLOCTEXT("SimpleQuestEditor", "AddPrereqOr", "Add OR"),
	        NSLOCTEXT("SimpleQuestEditor", "AddPrereqOrTooltip", "Any wired condition being satisfied is enough"),
	        0));
	    Action->NodeTemplate = NewObject<UQuestlineNode_PrerequisiteOr>(const_cast<UEdGraph*>(ContextMenuBuilder.CurrentGraph));
	    ContextMenuBuilder.AddAction(Action);
	}

	// Prereq NOT
	{
	    TSharedPtr<FEdGraphSchemaAction_NewNode> Action(new FEdGraphSchemaAction_NewNode(
	        NSLOCTEXT("SimpleQuestEditor", "PrereqCategory", "Prerequisite"),
	        NSLOCTEXT("SimpleQuestEditor", "AddPrereqNot", "Add NOT"),
	        NSLOCTEXT("SimpleQuestEditor", "AddPrereqNotTooltip", "Condition must NOT be satisfied"),
	        0));
	    Action->NodeTemplate = NewObject<UQuestlineNode_PrerequisiteNot>(const_cast<UEdGraph*>(ContextMenuBuilder.CurrentGraph));
	    ContextMenuBuilder.AddAction(Action);
	}

	// Prereq Group Setter
	{
	    TSharedPtr<FEdGraphSchemaAction_NewNode> Action(new FEdGraphSchemaAction_NewNode(
	        NSLOCTEXT("SimpleQuestEditor", "PrereqCategory", "Prerequisite"),
	        NSLOCTEXT("SimpleQuestEditor", "AddPrereqRuleEntry", "Add Prerequisite Rule Entry"),
			NSLOCTEXT("SimpleQuestEditor", "AddPrereqRuleEntryTooltip", "Defines a named rule — its Enter expression publishes a boolean under the rule's tag."),
	        0));
	    Action->NodeTemplate = NewObject<UQuestlineNode_PrerequisiteRuleEntry>(const_cast<UEdGraph*>(ContextMenuBuilder.CurrentGraph));
	    ContextMenuBuilder.AddAction(Action);
	}

	// Prereq Group Getter
	{
	    TSharedPtr<FEdGraphSchemaAction_NewNode> Action(new FEdGraphSchemaAction_NewNode(
	        NSLOCTEXT("SimpleQuestEditor", "PrereqCategory", "Prerequisite"),
	        NSLOCTEXT("SimpleQuestEditor", "AddPrereqRuleExit", "Add Prerequisite Rule Exit"),
			NSLOCTEXT("SimpleQuestEditor", "AddPrereqRuleExitTooltip", "Evaluates to the boolean published by a matching Prerequisite Rule Entry, from any graph."),
	        0));
	    Action->NodeTemplate = NewObject<UQuestlineNode_PrerequisiteRuleExit>(const_cast<UEdGraph*>(ContextMenuBuilder.CurrentGraph));
	    ContextMenuBuilder.AddAction(Action);
	}
	
	// ---- Flow Control ----
	// Block
	{
	    TSharedPtr<FEdGraphSchemaAction_NewNode> Action(new FEdGraphSchemaAction_NewNode(
	        NSLOCTEXT("SimpleQuestEditor", "FlowControlCategory", "Flow Control"),
	        NSLOCTEXT("SimpleQuestEditor", "AddSetBlocked", "Set Blocked"),
	        NSLOCTEXT("SimpleQuestEditor", "AddSetBlockedTooltip", "Deactivate and block one or more quests"),
	        0));
	    Action->NodeTemplate = NewObject<UQuestlineNode_SetBlocked>(const_cast<UEdGraph*>(ContextMenuBuilder.CurrentGraph));
	    ContextMenuBuilder.AddAction(Action);
	}
	// Unblock
	{
	    TSharedPtr<FEdGraphSchemaAction_NewNode> Action(new FEdGraphSchemaAction_NewNode(
	        NSLOCTEXT("SimpleQuestEditor", "FlowControlCategory", "Flow Control"),
	        NSLOCTEXT("SimpleQuestEditor", "AddClearBlocked", "Clear Blocked"),
	        NSLOCTEXT("SimpleQuestEditor", "AddClearBlockedTooltip", "Remove the blocked state from one or more quests"),
	        0));
	    Action->NodeTemplate = NewObject<UQuestlineNode_ClearBlocked>(const_cast<UEdGraph*>(ContextMenuBuilder.CurrentGraph));
	    ContextMenuBuilder.AddAction(Action);
	}

	// ---- Activation Group ----
	// Entry
	{
		TSharedPtr<FEdGraphSchemaAction_NewNode> Action(new FEdGraphSchemaAction_NewNode(
			NSLOCTEXT("SimpleQuestEditor", "ActivationGroupCategory", "Activation Group"),
			NSLOCTEXT("SimpleQuestEditor", "AddActivationGroupEntry", "Add Activation Group Entry"),
			NSLOCTEXT("SimpleQuestEditor", "AddActivationGroupEntryTooltip", "Publishes the group's activation tag. Every matching Exit in any graph fires in response."),
			0));
		Action->NodeTemplate = NewObject<UQuestlineNode_ActivationGroupEntry>(const_cast<UEdGraph*>(ContextMenuBuilder.CurrentGraph));
		ContextMenuBuilder.AddAction(Action);
	}
	// Exit
	{
		TSharedPtr<FEdGraphSchemaAction_NewNode> Action(new FEdGraphSchemaAction_NewNode(
			NSLOCTEXT("SimpleQuestEditor", "ActivationGroupCategory", "Activation Group"),
			NSLOCTEXT("SimpleQuestEditor", "AddActivationGroupExit", "Add Activation Group Exit"),
			NSLOCTEXT("SimpleQuestEditor", "AddActivationGroupExitTooltip", "Fires its output when any Entry with a matching group tag publishes, from any graph."),
			0));
		Action->NodeTemplate = NewObject<UQuestlineNode_ActivationGroupExit>(const_cast<UEdGraph*>(ContextMenuBuilder.CurrentGraph));
		ContextMenuBuilder.AddAction(Action);
	}
	
	// Add Comment — only when no pin is being dragged (comments don't participate in wiring).
	if (!ContextMenuBuilder.FromPin)
	{
		TSharedPtr<FEdGraphSchemaAction_NewNode> Action(new FEdGraphSchemaAction_NewNode(
			FText::GetEmpty(),
			NSLOCTEXT("SimpleQuestEditor", "AddComment", "Add Comment..."),
			NSLOCTEXT("SimpleQuestEditor", "AddCommentTooltip", "Create a comment box to group and annotate nodes"),
			0));
		Action->NodeTemplate = NewObject<UEdGraphNode_Comment>(const_cast<UEdGraph*>(ContextMenuBuilder.CurrentGraph));
		ContextMenuBuilder.AddAction(Action);
	}
}

void UQuestlineGraphSchema::GetContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const
{
	// Node-specific context menu actions go here
}

FLinearColor UQuestlineGraphSchema::GetPinTypeColor(const FEdGraphPinType& PinType) const
{
	if (PinType.PinCategory == TEXT("QuestOutcome"))
	{
		return SQ_ED_WIRE_OUTCOME;
	}
	if (PinType.PinCategory == TEXT("QuestDeactivated") || PinType.PinCategory == TEXT("QuestDeactivate"))
	{
		return SQ_ED_WIRE_DEACTIVATION;
	}
	return SQ_ED_WIRE_ACTIVATION;
}

bool UQuestlineGraphSchema::TryCreateConnection(UEdGraphPin* A, UEdGraphPin* B) const
{
    UEdGraphPin* OutputPin = (A->Direction == EGPD_Output) ? A : B;
    UEdGraphPin* InputPin  = (A->Direction == EGPD_Input)  ? A : B;

    // ── Self-loop: reuse existing arch if present, otherwise insert a new two-knot arch ──
    UEdGraphNode* OwningNode = OutputPin->GetOwningNode();
    if (OwningNode == InputPin->GetOwningNode())
    {
        if (UQuestlineNodeBase* QuestNode = Cast<UQuestlineNodeBase>(OwningNode))
        {
            const FName OutputCategory = OutputPin->PinType.PinCategory;
        	const bool bIsLegalSelfLoop = QuestNode->IsContentNode()
				&& UQuestlineNodeBase::GetPinRoleOf(InputPin) == EQuestPinRole::ExecIn
				&& (OutputCategory == TEXT("QuestOutcome")
					|| OutputCategory == TEXT("QuestActivation"));

        	if (!bIsLegalSelfLoop)
        	{
        		const bool bOk = Super::TryCreateConnection(A, B);
        		if (bOk)
        		{
        			if (UEdGraph* Graph = A->GetOwningNode()->GetGraph()) Graph->NotifyGraphChanged();
        		}
        		return bOk;
        	}

            // Run full connection validation before placing anything — the self-loop guard inside CanCreateConnection runs
            // dedupe via CheckDuplicateSources, so an AnyOutcome-vs-specific collision on the node's own Activate will be caught here
            // rather than after knot placement.
            const FPinConnectionResponse Response = CanCreateConnection(OutputPin, InputPin);
            if (Response.Response == CONNECT_RESPONSE_DISALLOW)
            {
                return false;
            }

            UEdGraph* Graph = OwningNode->GetGraph();
            Graph->Modify();

            // Look for an existing self-loop arch on this node. Criterion: a knot whose KnotOut connects back to this node's
            // Activate pin. If found, reuse its KnotIn as the shared sink: new outcome wires land there instead of spawning a parallel arch.
            UEdGraphPin* ReuseKnotInPin = nullptr;
            for (UEdGraphPin* ActivateLink : InputPin->LinkedTo)
            {
                const UQuestlineNode_Knot* LeftKnot = Cast<UQuestlineNode_Knot>(ActivateLink->GetOwningNode());
                if (!LeftKnot) continue;
                const UEdGraphPin* LeftIn = LeftKnot->FindPin(TEXT("KnotIn"));
                if (!LeftIn) continue;
                for (UEdGraphPin* LeftInLinked : LeftIn->LinkedTo)
                {
                    UQuestlineNode_Knot* RightKnot = Cast<UQuestlineNode_Knot>(LeftInLinked->GetOwningNode());
                    if (!RightKnot) continue;
                    // Confirm the arch upstream side: RightKnot's KnotIn has at least one link to this node.
                    const UEdGraphPin* RightIn = RightKnot->FindPin(TEXT("KnotIn"));
                    if (!RightIn) continue;
                    for (const UEdGraphPin* UpstreamLink : RightIn->LinkedTo)
                    {
                        if (UpstreamLink->GetOwningNode() == OwningNode)
                        {
                            ReuseKnotInPin = RightKnot->FindPin(TEXT("KnotIn"));
                            break;
                        }
                    }
                    if (ReuseKnotInPin) break;
                }
                if (ReuseKnotInPin) break;
            }

        	if (ReuseKnotInPin)
        	{
        		const bool bOk = Super::TryCreateConnection(OutputPin, ReuseKnotInPin);
        		if (bOk && Graph) Graph->NotifyGraphChanged();
        		return bOk;
        	}

            // No existing arch — build one.
            const float NodeWidth = 200.f;
            const float KnotOffset = 60.f;

            auto MakeKnot = [&](float X, float Y) -> UQuestlineNode_Knot*
            {
                FGraphNodeCreator<UQuestlineNode_Knot> Creator(*Graph);
                UQuestlineNode_Knot* Knot = Creator.CreateNode();
                Knot->NodePosX = X;
                Knot->NodePosY = Y;
                Creator.Finalize();
                if (UEdGraphPin* In  = Knot->FindPin(TEXT("KnotIn")))
                {
                    In->PinType = OutputPin->PinType;
                }
                if (UEdGraphPin* Out = Knot->FindPin(TEXT("KnotOut")))
                {
                    Out->PinType = OutputPin->PinType;
                }
                return Knot;
            };

            UQuestlineNode_Knot* KnotRight = MakeKnot(OwningNode->NodePosX + NodeWidth, OwningNode->NodePosY - KnotOffset);
            UQuestlineNode_Knot* KnotLeft = MakeKnot(OwningNode->NodePosX, OwningNode->NodePosY - KnotOffset);

        	Super::TryCreateConnection(OutputPin, KnotRight->FindPin(TEXT("KnotIn")));
        	Super::TryCreateConnection(KnotRight->FindPin(TEXT("KnotOut")), KnotLeft->FindPin(TEXT("KnotIn")));
        	Super::TryCreateConnection(KnotLeft->FindPin(TEXT("KnotOut")), InputPin);

        	if (Graph) Graph->NotifyGraphChanged();
        	return true;
        }
    }
	const bool bOk = Super::TryCreateConnection(A, B);
	if (bOk)
	{
		if (UEdGraph* Graph = A->GetOwningNode()->GetGraph()) Graph->NotifyGraphChanged();
	}
	return bOk;
}

// File-local storage — never crosses DLL boundaries
static TFunction<FConnectionDrawingPolicy*(int32, int32, float, const FSlateRect&, FSlateWindowElementList&, UEdGraph*)>
	GENPolicyFactory = nullptr;

void UQuestlineGraphSchema::RegisterENPolicyFactory(
	TFunction<FConnectionDrawingPolicy*(int32, int32, float, const FSlateRect&, FSlateWindowElementList&, UEdGraph*)> Factory)
{
	GENPolicyFactory = MoveTemp(Factory);
}

void UQuestlineGraphSchema::UnregisterENPolicyFactory()
{
	GENPolicyFactory = nullptr;
}

bool UQuestlineGraphSchema::IsENPolicyFactoryActive()
{
	return static_cast<bool>(GENPolicyFactory);
}

static TWeakObjectPtr<UEdGraphNode> GActiveDragFromNode;
static FName GActiveDragFromPinName;

void UQuestlineGraphSchema::SetActiveDragFromPin(UEdGraphPin* Pin)
{
	if (Pin)
	{
		GActiveDragFromNode = Pin->GetOwningNode();
		GActiveDragFromPinName = Pin->PinName;
	}
	else
	{
		GActiveDragFromNode = nullptr;
		GActiveDragFromPinName = NAME_None;
	}
}

UEdGraphPin* UQuestlineGraphSchema::GetActiveDragFromPin()
{
	if (UEdGraphNode* Node = GActiveDragFromNode.Get())
	{
		return Node->FindPin(GActiveDragFromPinName);
	}
	return nullptr;
}

void UQuestlineGraphSchema::ClearActiveDragFromPin()
{
	GActiveDragFromNode = nullptr;
	GActiveDragFromPinName = NAME_None;
}

FConnectionDrawingPolicy* UQuestlineGraphSchema::CreateConnectionDrawingPolicy(int32 InBackLayerID, int32 InFrontLayerID,
	float InZoomFactor, const FSlateRect& InClippingRect, FSlateWindowElementList& InDrawElements, UEdGraph* InGraphObj) const
{
	if (GENPolicyFactory != nullptr)
	{
		if (FConnectionDrawingPolicy* Policy = GENPolicyFactory(InBackLayerID, InFrontLayerID, InZoomFactor, InClippingRect, InDrawElements, InGraphObj))
		{
			return Policy;
		}
	}

	return new FQuestlineConnectionDrawingPolicy(InBackLayerID, InFrontLayerID, InZoomFactor, InClippingRect, InDrawElements, InGraphObj);
}



