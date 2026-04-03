// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Graph/QuestlineGraphSchema.h"

#include "BlueprintConnectionDrawingPolicy.h"
#include "Nodes/QuestlineNode_Entry.h"
#include "Nodes/QuestlineNode_Knot.h"
#include "Nodes/QuestlineNode_Quest.h"
#include "Utilities/SimpleQuestEditorUtils.h"
#include "ConnectionDrawingPolicy.h"
#include "EdGraphUtilities.h"
#include "Nodes/QuestlineNode_Exit_Failure.h"
#include "Nodes/QuestlineNode_Exit_Success.h"
#include "ScopedTransaction.h"
#include "Graph/QuestlineDrawingPolicyMixin.h"
#include "Nodes/QuestlineNode_Leaf.h"
#include "Nodes/QuestlineNode_LinkedQuestline.h"


// Returns true if OutputPin already leads to any exit node (directly or via reroutes). Enforces: one output pin → at
// most one exit node.
static bool HasDownstreamExit(const UEdGraphPin* OutputPin, TSet<const UEdGraphNode*>& Visited)
{
	if (!OutputPin) return false;
	for (const UEdGraphPin* Connected : OutputPin->LinkedTo)
	{
		if (!Connected) continue;
		const UEdGraphNode* Node = Connected->GetOwningNode();
		if (Visited.Contains(Node)) continue;
		Visited.Add(Node);
		if (Cast<UQuestlineNode_ExitBase>(Node)) return true;
		if (const UQuestlineNode_Knot* Knot = Cast<UQuestlineNode_Knot>(Node))
		{
			if (HasDownstreamExit(Knot->FindPin(TEXT("KnotOut")), Visited)) return true;
		}
	}
	return false;
}

// Returns true if OutputPin already leads to any content node (Quest, Leaf, or LinkedQuestline), directly or via reroutes.
static bool HasDownstreamContent(const UEdGraphPin* OutputPin, TSet<const UEdGraphNode*>& Visited)
{
	if (!OutputPin) return false;
	for (const UEdGraphPin* Connected : OutputPin->LinkedTo)
	{
		if (!Connected) continue;
		const UEdGraphNode* Node = Connected->GetOwningNode();
		if (Visited.Contains(Node)) continue;
		Visited.Add(Node);
		if (Cast<const UQuestlineNode_Quest>(Node)
			|| Cast<const UQuestlineNode_Leaf>(Node)
			|| Cast<const UQuestlineNode_LinkedQuestline>(Node))
			return true;
		if (const UQuestlineNode_Knot* Knot = Cast<const UQuestlineNode_Knot>(Node))
		{
			if (HasDownstreamContent(Knot->FindPin(TEXT("KnotOut")), Visited)) return true;
		}
	}
	return false;
}

// Returns true if OutputPin leads to TargetNode (directly or via reroutes). Used to check whether a source quest node
// already has a path to a given exit node.
static bool LeadsToNode(const UEdGraphPin* OutputPin, const UEdGraphNode* TargetNode, TSet<const UEdGraphNode*>& Visited)
{
	if (!OutputPin) return false;
	for (const UEdGraphPin* ConnectedPin : OutputPin->LinkedTo)
	{
		if (!ConnectedPin) continue;
		const UEdGraphNode* Node = ConnectedPin->GetOwningNode();
		if (Node == TargetNode) return true;
		if (Visited.Contains(Node)) continue;
		Visited.Add(Node);
		if (const UQuestlineNode_Knot* Knot = Cast<const UQuestlineNode_Knot>(Node))
		{
			if (LeadsToNode(Knot->FindPin(TEXT("KnotOut")), TargetNode, Visited)) return true;
		}
	}
	return false;
}

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

	/*
	virtual void Draw(TMap<TSharedRef<SWidget>, FArrangedWidget>& InPinGeometries, FArrangedChildren& ArrangedNodes) override
	{
		FConnectionDrawingPolicy::Draw(InPinGeometries, ArrangedNodes);
	}
	*/
	
	virtual void DrawConnection(int32 LayerId, const FVector2f& Start, const FVector2f& End, const FConnectionParams& Params) override
	{		
		const FVector2f SplineTangent = ComputeSplineTangent(Start, End);
		const FVector2f P0Tangent = Params.StartTangent.IsNearlyZero() /* ? DirectionalSplineTangent : Params.StartTangent; */
			? ((Params.StartDirection == EGPD_Output) ? SplineTangent : -SplineTangent)
			: Params.StartTangent;
		const FVector2f P1Tangent = Params.EndTangent.IsNearlyZero() /* ? DirectionalSplineTangent : Params.EndTangent; */
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
	KnotIn->PinType  = OutputPin->PinType;
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

void UQuestlineGraphSchema::CollectSourceNodes(const UEdGraphPin* Pin, TSet<UQuestlineNode_ContentBase*>& OutSources,
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
				CollectSourceNodes(Linked, OutSources, Visited);
		}
	}
}

void UQuestlineGraphSchema::CollectDownstreamTerminalInputs(const UEdGraphPin* KnotOutPin, TArray<const UEdGraphPin*>& OutTerminalPins,
	TSet<const UEdGraphNode*>& Visited)
{
	if (!KnotOutPin) return;
	for (const UEdGraphPin* Connected : KnotOutPin->LinkedTo)
	{
		if (!Connected) continue;
		const UEdGraphNode* Node = Connected->GetOwningNode();
		if (Visited.Contains(Node)) continue;
		Visited.Add(Node);
		if (const UQuestlineNode_Knot* NextKnot = Cast<const UQuestlineNode_Knot>(Node))
			CollectDownstreamTerminalInputs(NextKnot->FindPin(TEXT("KnotOut")), OutTerminalPins, Visited);
		else
			OutTerminalPins.Add(Connected);
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

	// Exit Node enforcement
	if (Cast<UQuestlineNode_ExitBase>(InputNode))
	{
		// Exit Rule 1: a quest node may not have more than one output pin leading to the same exit node — trace back to all
		// source quests and check their other outputs for an existing path to this exit
		TSet<UQuestlineNode_ContentBase*> QuestSources;
		{
			TSet<const UEdGraphNode*> Visited;
			CollectSourceNodes(OutputPin, QuestSources, Visited);
		}
		for (UQuestlineNode_ContentBase* QuestSourceNode : QuestSources)
		{
			for (UEdGraphPin* SourcePin : QuestSourceNode->Pins)
			{
				if (SourcePin->Direction != EGPD_Output) continue;
				TSet<const UEdGraphNode*> Visited;
				if (LeadsToNode(SourcePin, InputNode, Visited))
				{
					return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, NSLOCTEXT("SimpleQuestEditor", "QuestAlreadyToExit",
						"A quest node in the intended connection is already connected to this Questline end node."));
				}
			}
		}
		// Rule 2: the output pin being connected may not already lead to any exit node — one output pin maps to at most one exit node
		{
			TSet<const UEdGraphNode*> Visited;
			if (HasDownstreamExit(OutputPin, Visited))
			{
				return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, NSLOCTEXT("SimpleQuestEditor", "OutputAlreadyLeadsToExit",
					"A Quest on this wire already leads to a Questline end node. Each outcome can only trigger one ending."));
			}
		}
		// Rule 3: the output pin cannot already lead to a content node — exit and progression are mutually exclusive
		{
			TSet<const UEdGraphNode*> Visited;
			if (HasDownstreamContent(OutputPin, Visited))
			{
				return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, NSLOCTEXT("SimpleQuestEditor", "ExitMixedWithContent",
					"This output already activates a Quest node. A wire cannot both progress and end the questline."));
			}
		}
	}
	
	// Lambda for checking upstream connections
	auto CheckDuplicateSources = [&]() -> FPinConnectionResponse
	{
		TSet<UQuestlineNode_ContentBase*> IncomingSources;
		{
			TSet<const UEdGraphNode*> Visited;
			CollectSourceNodes(OutputPin, IncomingSources, Visited);
		}
		if (IncomingSources.Num() > 0)
		{
			for (const UEdGraphPin* Existing : InputPin->LinkedTo)
			{
				TSet<UQuestlineNode_ContentBase*> ExistingSources;
				TSet<const UEdGraphNode*> Visited;
				CollectSourceNodes(Existing, ExistingSources, Visited);

				if (int32 NumCollisions = IncomingSources.Intersect(ExistingSources).Num(); NumCollisions > 0)
				{
					FText DescriptionText;
					if (bOutputIsKnot && IncomingSources.Num() > 1)
					{
						DescriptionText = NumCollisions > 1
						? NSLOCTEXT("SimpleQuestEditor", "DuplicateSourceKnotMultiple", "This input already receives a signal from some of those Quest nodes.")
						: NSLOCTEXT("SimpleQuestEditor", "DuplicateSourceKnotSingle",   "This input already receives a signal from one of those Quest nodes.");
					}
					else
					{
						DescriptionText = NSLOCTEXT("SimpleQuestEditor", "DuplicateSourceKnotMultiple", "This input already receives a signal from that Quest node.");
					}
					return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, DescriptionText);
				}
			}
		}
		return FPinConnectionResponse(CONNECT_RESPONSE_MAKE, FText::GetEmpty());
	};

	// Lambda for checking downstream connections
	auto CheckDownstreamParallelPaths = [&](const UEdGraphPin* KnotInputPin) -> FPinConnectionResponse
	{
		TSet<UQuestlineNode_ContentBase*> IncomingSources;
		{
			TSet<const UEdGraphNode*> Visited;
			CollectSourceNodes(OutputPin, IncomingSources, Visited);
		}
		if (IncomingSources.Num() == 0)
			return FPinConnectionResponse(CONNECT_RESPONSE_MAKE, FText::GetEmpty());

		const UQuestlineNode_Knot* InputKnot = Cast<const UQuestlineNode_Knot>(KnotInputPin->GetOwningNode());
		TArray<const UEdGraphPin*> DownstreamTerminals;
		{
			TSet<const UEdGraphNode*> Visited;
			Visited.Add(KnotInputPin->GetOwningNode());
			CollectDownstreamTerminalInputs(InputKnot->FindPin(TEXT("KnotOut")), DownstreamTerminals, Visited);
		}
		for (const UEdGraphPin* Terminal : DownstreamTerminals)
		{
			for (const UEdGraphPin* Existing : Terminal->LinkedTo)
			{
				TSet<UQuestlineNode_ContentBase*> ExistingSources;
				TSet<const UEdGraphNode*> Visited;
				CollectSourceNodes(Existing, ExistingSources, Visited);
				if (IncomingSources.Intersect(ExistingSources).Num() > 0)
				{
					return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW,
						NSLOCTEXT("SimpleQuestEditor", "DuplicatePathViaReroute",
							"This would create a parallel path to a destination node via another connection."));
				}
			}
		}
		return FPinConnectionResponse(CONNECT_RESPONSE_MAKE, FText::GetEmpty());
	};

	// Mixed-terminal guard: connecting to a content node is illegal if the output already leads to an exit
	if (Cast<const UQuestlineNode_ContentBase>(InputNode))
	{
		TSet<const UEdGraphNode*> Visited;
		if (HasDownstreamExit(OutputPin, Visited))
		{
			return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, NSLOCTEXT("SimpleQuestEditor", "ContentMixedWithExit",
				"This output already leads to an exit node. A wire cannot both end and progress the questline."));
		}
	}
	
	// Either the input or the output pin is a knot (reroute node) 
	if (bOutputIsKnot || bInputIsKnot)
	{
		// Both are knots
		if (bOutputIsKnot && bInputIsKnot)
		{
			// It's the same knot 
			if (OutputNode == InputNode)
			{
				return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW,
					NSLOCTEXT("SimpleQuestEditor", "KnotSelfLoop", "Cannot connect a reroute node to itself"));
			}

			// Only enforce type consistency if the target knot's KnotIn already has connections.
			// An empty KnotIn accepts the first wire of any color (which then locks its type).
			if (InputPin->LinkedTo.Num() > 0)
			{
				const FName InCat  = Cast<const UQuestlineNode_Knot>(InputNode)->GetEffectiveCategory();
				const FName OutCat = Cast<const UQuestlineNode_Knot>(OutputNode)->GetEffectiveCategory();
				if (InCat != OutCat)
				{
					return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW,
						NSLOCTEXT("SimpleQuestEditor", "KnotTypeMismatch", "Reroute node signal types do not match"));
				}
			}

			// Mixed-terminal guard: merging two knot paths must not produce a wire that leads to both an exit and a content node
			{
				if (UEdGraphPin* KnotOutPin = Cast<const UQuestlineNode_Knot>(InputNode)
						? const_cast<UQuestlineNode_Knot*>(Cast<const UQuestlineNode_Knot>(InputNode))->FindPin(TEXT("KnotOut"))
						: nullptr)
				{
					TSet<const UEdGraphNode*> VA; const bool bFromExit    = HasDownstreamExit(OutputPin, VA);
					TSet<const UEdGraphNode*> VB; const bool bFromContent = HasDownstreamContent(OutputPin, VB);
					TSet<const UEdGraphNode*> VC; const bool bToExit      = HasDownstreamExit(KnotOutPin, VC);
					TSet<const UEdGraphNode*> VD; const bool bToContent   = HasDownstreamContent(KnotOutPin, VD);

					if ((bFromExit && bToContent) || (bFromContent && bToExit))
					{
						return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, NSLOCTEXT("SimpleQuestEditor", "KnotMergesMixed",
							"This connection would merge an exit path with a progression path on the same wire."));
					}
				}
			}
			
			if (const FPinConnectionResponse R = CheckDuplicateSources(); R.Response != CONNECT_RESPONSE_MAKE) return R;
			return CheckDownstreamParallelPaths(InputPin);
		}

		if (bInputIsKnot)
		{
			// Only enforce type consistency if the target knot's KnotIn already has connections.
			// An empty KnotIn accepts the first wire of any color (which then locks its type).
			if (InputPin->LinkedTo.Num() > 0)
			{
				const FName KnotCategory = Cast<const UQuestlineNode_Knot>(InputNode)->GetEffectiveCategory();
				const FName WireCategory = OutputPin->PinType.PinCategory;
				if (KnotCategory != WireCategory)
				{
					return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW,
						NSLOCTEXT("SimpleQuestEditor", "KnotTypeMismatch", "Signal type does not match this reroute node"));
				}
			}
			
			// Mixed-terminal guard: merging two knot paths must not produce a wire that leads to both an exit and a content node
			{
				if (UEdGraphPin* KnotOutPin = Cast<const UQuestlineNode_Knot>(InputNode)
						? const_cast<UQuestlineNode_Knot*>(Cast<const UQuestlineNode_Knot>(InputNode))->FindPin(TEXT("KnotOut"))
						: nullptr)
				{
					TSet<const UEdGraphNode*> VA; const bool bFromExit    = HasDownstreamExit(OutputPin, VA);
					TSet<const UEdGraphNode*> VB; const bool bFromContent = HasDownstreamContent(OutputPin, VB);
					TSet<const UEdGraphNode*> VC; const bool bToExit      = HasDownstreamExit(KnotOutPin, VC);
					TSet<const UEdGraphNode*> VD; const bool bToContent   = HasDownstreamContent(KnotOutPin, VD);

					if ((bFromExit && bToContent) || (bFromContent && bToExit))
					{
						return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, NSLOCTEXT("SimpleQuestEditor", "KnotMergesMixed",
							"This connection would merge an exit path with a progression path on the same wire."));
					}
				}
			}

			if (const FPinConnectionResponse R = CheckDuplicateSources(); R.Response != CONNECT_RESPONSE_MAKE) return R;
			return CheckDownstreamParallelPaths(InputPin);
		}

		// bOutputIsKnot, non-knot destination — fall through to duplicate check
	}
	else
	{
		auto IsQuestLikeNode = [](const UEdGraphNode* Node) -> bool
		{
			return Cast<const UQuestlineNode_Quest>(Node)
				|| Cast<const UQuestlineNode_Leaf>(Node)
				|| Cast<const UQuestlineNode_LinkedQuestline>(Node);
		};

		if (OutputNode == InputNode)
		{
			if (IsQuestLikeNode(OutputNode) && InputPin->PinName == TEXT("Activate"))
			{
				return FPinConnectionResponse(CONNECT_RESPONSE_MAKE, FText::GetEmpty());
			}
			return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW,
				NSLOCTEXT("SimpleQuestEditor", "SameNode", "Cannot connect a node to its own Prerequisites pin"));
		}

		if (Cast<const UQuestlineNode_Entry>(OutputNode))
		{
			if (!IsQuestLikeNode(InputNode))
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

		if (IsQuestLikeNode(OutputNode))
		{
			const bool bValidInput = IsQuestLikeNode(InputNode)
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

	return CheckDuplicateSources();
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
	
	// Add Leaf node action
	{
		TSharedPtr<FEdGraphSchemaAction_NewNode> Action(new FEdGraphSchemaAction_NewNode(
			FText::GetEmpty(),
			NSLOCTEXT("SimpleQuestEditor", "AddLeafNode", "Add Quest Step"),
			NSLOCTEXT("SimpleQuestEditor", "AddLeafNodeTooltip", "Add a Quest Step leaf node"),
			0));
		Action->NodeTemplate = NewObject<UQuestlineNode_Leaf>(
			const_cast<UEdGraph*>(ContextMenuBuilder.CurrentGraph));
		ContextMenuBuilder.AddAction(Action);
	}

	// Add Linked Questline node action
	{
		TSharedPtr<FEdGraphSchemaAction_NewNode> Action(new FEdGraphSchemaAction_NewNode(
			FText::GetEmpty(),
			NSLOCTEXT("SimpleQuestEditor", "AddLinkedNode", "Add Linked Questline"),
			NSLOCTEXT("SimpleQuestEditor", "AddLinkedNodeTooltip", "Reference an external questline graph asset"),
			0));
		Action->NodeTemplate = NewObject<UQuestlineNode_LinkedQuestline>(
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
		return SQ_ED_GREEN;		// green
	}
	if (PinType.PinCategory == TEXT("QuestFailure"))
	{
		return SQ_ED_RED;		// red
	}
	return FLinearColor::White;		// QuestActivation — Activate, Prerequisites, AnyOutcome, Start, knots
}

/* Automatic reroute node placement when looping back to the same node -- currently always feeds wire into left side of node, needs a fix to create a clean loop */

/*
bool UQuestlineGraphSchema::TryCreateConnection(UEdGraphPin* A, UEdGraphPin* B) const
{
	UEdGraphPin* OutputPin = A->Direction == EGPD_Output ? A : B;
	UEdGraphPin* InputPin = A->Direction == EGPD_Input ? A : B;

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
			if (UEdGraphPin* In = Knot->FindPin(TEXT("KnotIn"))) In->PinType = OutputPin->PinType;
			if (UEdGraphPin* Out = Knot->FindPin(TEXT("KnotOut"))) Out->PinType = OutputPin->PinType;
			return Knot;
		};

		UQuestlineNode_Knot* KnotRight = MakeKnot(QuestNode->NodePosX + NodeWidth, QuestNode->NodePosY - KnotOffset);
		UQuestlineNode_Knot* KnotLeft = MakeKnot(QuestNode->NodePosX, QuestNode->NodePosY - KnotOffset);

		Super::TryCreateConnection(OutputPin, KnotRight->FindPin(TEXT("KnotIn")));
		Super::TryCreateConnection(KnotRight->FindPin(TEXT("KnotOut")), KnotLeft->FindPin(TEXT("KnotIn")));
		Super::TryCreateConnection(KnotLeft->FindPin(TEXT("KnotOut")), InputPin);

		return true;
	}

	return Super::TryCreateConnection(A, B);
}
*/

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
		return Node->FindPin(GActiveDragFromPinName);
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
			return Policy;
	}

	return new FQuestlineConnectionDrawingPolicy(InBackLayerID, InFrontLayerID, InZoomFactor, InClippingRect, InDrawElements, InGraphObj);
}



