// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Graph/QuestlineGraphSchema.h"
#include "Graph/QuestlineDrawingPolicyMixin.h"
#include "BlueprintConnectionDrawingPolicy.h"
#include "Nodes/QuestlineNode_Entry.h"
#include "Nodes/QuestlineNode_Knot.h"
#include "Nodes/QuestlineNode_Quest.h"
#include "Nodes/QuestlineNode_Exit_Failure.h"
#include "Nodes/QuestlineNode_Exit_Success.h"
#include "Nodes/QuestlineNode_Step.h"
#include "Nodes/QuestlineNode_LinkedQuestline.h"
#include "Nodes/Prerequisites/QuestlineNode_PrerequisiteAnd.h"
#include "Nodes/Prerequisites/QuestlineNode_PrerequisiteOr.h"
#include "Nodes/Prerequisites/QuestlineNode_PrerequisiteNot.h"
#include "Nodes/Prerequisites/QuestlineNode_PrerequisiteGroupSetter.h"
#include "Nodes/Prerequisites/QuestlineNode_PrerequisiteGroupGetter.h"
#include "Utilities/SimpleQuestEditorUtils.h"
#include "ConnectionDrawingPolicy.h"
#include "EdGraphUtilities.h"
#include "ScopedTransaction.h"



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

const FPinConnectionResponse UQuestlineGraphSchema::CanCreateConnection(const UEdGraphPin* A, const UEdGraphPin* B) const
{
    if (!A || !B)
        return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, NSLOCTEXT("SimpleQuestEditor", "PinNull", "Invalid pin"));

    if (A->Direction == B->Direction)
        return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, NSLOCTEXT("SimpleQuestEditor", "SameDirection", "Cannot connect two inputs or two outputs"));

    const UEdGraphPin*  OutputPin  = (A->Direction == EGPD_Output) ? A : B;
    const UEdGraphPin*  InputPin   = (A->Direction == EGPD_Input)  ? A : B;
    const UEdGraphNode* OutputNode = OutputPin->GetOwningNode();
    const UEdGraphNode* InputNode  = InputPin->GetOwningNode();

    const bool bOutputIsKnot = TraversalPolicy->IsPassThroughNode(OutputNode);
    const bool bInputIsKnot  = TraversalPolicy->IsPassThroughNode(InputNode);

	// ---- Prerequisites ---- 
	const bool bOutputIsPrereq = (OutputPin->PinType.PinCategory == TEXT("QuestPrerequisite"));
	const bool bInputIsPrereq  = (InputPin->PinType.PinCategory  == TEXT("QuestPrerequisite"));
	if ((bOutputIsPrereq || bInputIsPrereq) && !bOutputIsKnot && !bInputIsKnot)
	{
		// A QuestPrerequisite input pin may only carry one wire. Use an AND or an OR node to combine conditions
		if (bInputIsPrereq && InputPin->Direction == EGPD_Input && InputPin->LinkedTo.Num() > 0)
		{
			return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, NSLOCTEXT("SimpleQuestEditor", "PrerequisiteSingleInput",
				"This prerequisite input already has a connection. Use an AND or OR node to combine conditions."));
		}

		// prereq output to prereq input: operator chaining or getter → operator
		if (bOutputIsPrereq && bInputIsPrereq)
			return FPinConnectionResponse(CONNECT_RESPONSE_MAKE, FText::GetEmpty());

		// prereq output to non-prereq input: only the Prerequisites pin on a content node
		if (bOutputIsPrereq)
		{
			if (InputPin->PinName == TEXT("Prerequisites") && TraversalPolicy->IsContentNode(InputNode))
			{
				return FPinConnectionResponse(CONNECT_RESPONSE_MAKE, FText::GetEmpty());
			}
			return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, NSLOCTEXT("SimpleQuestEditor", "PrereqOutputInvalid",
				"Prerequisite outputs may only connect to the Prerequisites pin or other prerequisite nodes"));
		}

		// non-prereq output to prereq input: only Quest outcome pins (Success, Failure, Any Outcome)
		const FName OutputCategory = OutputPin->PinType.PinCategory;
		const bool bIsQuestOutcome =
			OutputCategory == TEXT("QuestSuccess") ||
			OutputCategory == TEXT("QuestFailure") ||
			(OutputCategory == TEXT("QuestActivation") && OutputPin->PinName == TEXT("Any Outcome"));

		if (bIsQuestOutcome && TraversalPolicy->IsContentNode(OutputNode))
			return FPinConnectionResponse(CONNECT_RESPONSE_MAKE, FText::GetEmpty());

		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW,
			NSLOCTEXT("SimpleQuestEditor", "PrereqInputInvalid",
				"Prerequisite inputs may only accept connections from Quest outcome pins"));
	}
	
    // ---- Exit node enforcement ----
    if (TraversalPolicy->IsExitNode(InputNode))
    {
        // Rule 1: no source quest node may already have a separate path to this specific exit node
        TSet<UQuestlineNode_ContentBase*> QuestSources;
        { TSet<const UEdGraphNode*> V; TraversalPolicy->CollectSourceContentNodes(OutputPin, QuestSources, V); }
        for (UQuestlineNode_ContentBase* QuestSourceNode : QuestSources)
        {
            for (UEdGraphPin* SourcePin : QuestSourceNode->Pins)
            {
                if (SourcePin->Direction != EGPD_Output) continue;
                TSet<const UEdGraphNode*> V;
                if (TraversalPolicy->LeadsToNode(SourcePin, InputNode, V))
                    return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, NSLOCTEXT("SimpleQuestEditor", "QuestAlreadyToExit",
                        "A quest node in the intended connection is already connected to this Questline end node."));
            }
        }

        const FQuestlineGraphTraversalPolicy::FPinReachability R = TraversalPolicy->ComputeFullReachability(OutputPin, OutputNode);
        if (R.bReachesExit)
            return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, NSLOCTEXT("SimpleQuestEditor", "OutputAlreadyLeadsToExit",
                "A Quest on this wire already leads to a Questline end node. Each outcome can only trigger one ending."));
        if (R.bReachesContent)
            return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, NSLOCTEXT("SimpleQuestEditor", "ExitMixedWithContent",
                "This output already activates a Quest node. A wire cannot both progress and end the questline."));
    }

    // ---- Content node mixed-terminal guard ----
    if (TraversalPolicy->IsContentNode(InputNode))
    {
        if (TraversalPolicy->ComputeFullReachability(OutputPin, OutputNode).bReachesExit)
            return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, NSLOCTEXT("SimpleQuestEditor", "ContentMixedWithExit",
                "This output already leads to an exit node. A wire cannot both end and progress the questline."));
    }

    // ---- Duplicate-source lambda ----
    auto CheckDuplicateSources = [&]() -> FPinConnectionResponse
    {
        TSet<UQuestlineNode_ContentBase*> IncomingSources;
        { TSet<const UEdGraphNode*> V; TraversalPolicy->CollectSourceContentNodes(OutputPin, IncomingSources, V); }
        if (IncomingSources.Num() > 0)
        {
            for (const UEdGraphPin* Existing : InputPin->LinkedTo)
            {
                TSet<UQuestlineNode_ContentBase*> ExistingSources;
                TSet<const UEdGraphNode*> V;
                TraversalPolicy->CollectSourceContentNodes(Existing, ExistingSources, V);
                if (int32 NumCollisions = IncomingSources.Intersect(ExistingSources).Num(); NumCollisions > 0)
                {
                    FText Msg;
                    if (bOutputIsKnot && IncomingSources.Num() > 1)
                    {
                        Msg = NumCollisions > 1
                            ? NSLOCTEXT("SimpleQuestEditor", "DuplicateSourceKnotMultiple", "This input already receives a signal from some of those Quest nodes.")
                            : NSLOCTEXT("SimpleQuestEditor", "DuplicateSourceKnotSingle",   "This input already receives a signal from one of those Quest nodes.");
                    }
                    else
                    {
                        Msg = NSLOCTEXT("SimpleQuestEditor", "DuplicateSource", "This input already receives a signal from that Quest node.");
                    }
                    return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, Msg);
                }
            }
        }
        return FPinConnectionResponse(CONNECT_RESPONSE_MAKE, FText::GetEmpty());
    };

    // ---- Downstream parallel-path lambda ----
    auto CheckDownstreamParallelPaths = [&](const UEdGraphPin* KnotInputPin) -> FPinConnectionResponse
    {
        TSet<UQuestlineNode_ContentBase*> IncomingSources;
        { TSet<const UEdGraphNode*> V; TraversalPolicy->CollectSourceContentNodes(OutputPin, IncomingSources, V); }
        if (IncomingSources.Num() == 0)
            return FPinConnectionResponse(CONNECT_RESPONSE_MAKE, FText::GetEmpty());

        const UEdGraphNode* KnotNode = KnotInputPin->GetOwningNode();
        TArray<const UEdGraphPin*> DownstreamTerminals;
        {
            TSet<const UEdGraphNode*> V;
            V.Add(KnotNode);
            TraversalPolicy->CollectDownstreamTerminalInputs(TraversalPolicy->GetPassThroughOutputPin(KnotNode), DownstreamTerminals, V);
        }
        for (const UEdGraphPin* Terminal : DownstreamTerminals)
        {
            for (const UEdGraphPin* Existing : Terminal->LinkedTo)
            {
                TSet<UQuestlineNode_ContentBase*> ExistingSources;
                TSet<const UEdGraphNode*> V;
                TraversalPolicy->CollectSourceContentNodes(Existing, ExistingSources, V);
                if (IncomingSources.Intersect(ExistingSources).Num() > 0)
                    return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW,
                        NSLOCTEXT("SimpleQuestEditor", "DuplicatePathViaReroute",
                            "This would create a parallel path to a destination node via another connection."));
            }
        }
        return FPinConnectionResponse(CONNECT_RESPONSE_MAKE, FText::GetEmpty());
    };

    // ---- Knot (reroute) node handling ----
    if (bOutputIsKnot || bInputIsKnot)
    {
        if (bOutputIsKnot && bInputIsKnot && OutputNode == InputNode)
            return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW,
                NSLOCTEXT("SimpleQuestEditor", "KnotSelfLoop", "Cannot connect a reroute node to itself"));

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

        	// Prerequisite wires may not fan into reroute nodes — use AND node to combine conditions
        	const FName EffectiveCategory = Cast<const UQuestlineNode_Knot>(InputNode)->GetEffectiveCategory();
        	if (EffectiveCategory == TEXT("QuestPrerequisite"))
        	{
        		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW,
					NSLOCTEXT("SimpleQuestEditor", "PrerequisiteRerouteMultiple", "Use an AND node to combine prerequisite conditions."));
        	}
        }

        if (bInputIsKnot)
        {
            if (const UEdGraphPin* KnotOutPin = TraversalPolicy->GetPassThroughOutputPin(InputNode))
            {
                const FQuestlineGraphTraversalPolicy::FPinReachability From = TraversalPolicy->ComputeFullReachability(OutputPin, OutputNode);
                const FQuestlineGraphTraversalPolicy::FPinReachability To   = TraversalPolicy->ComputeForwardReachability(KnotOutPin);
                if ((From.bReachesExit && To.bReachesContent) || (From.bReachesContent && To.bReachesExit))
                    return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, NSLOCTEXT("SimpleQuestEditor", "KnotMergesMixed",
                        "This connection would merge an exit path with a progression path on the same wire."));
            }
            if (const FPinConnectionResponse R = CheckDuplicateSources(); R.Response != CONNECT_RESPONSE_MAKE) return R;
            return CheckDownstreamParallelPaths(InputPin);
        }
    }
    else
    {
        auto IsQuestLikeNode = [&](const UEdGraphNode* Node) -> bool
        {
            return TraversalPolicy->IsContentNode(Node);
        };

        if (OutputNode == InputNode)
        {
            if (IsQuestLikeNode(OutputNode) && InputPin->PinName == TEXT("Activate"))
                return FPinConnectionResponse(CONNECT_RESPONSE_MAKE, FText::GetEmpty());
            return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW,
                NSLOCTEXT("SimpleQuestEditor", "SameNode", "Cannot connect a node to its own Prerequisites pin"));
        }

        if (Cast<const UQuestlineNode_Entry>(OutputNode))
        {
            if (!IsQuestLikeNode(InputNode))
                return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW,
                    NSLOCTEXT("SimpleQuestEditor", "EntryOnlyToQuest", "Quest Start may only connect to a Quest node"));
            if (InputPin->PinName != TEXT("Activate"))
                return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW,
                    NSLOCTEXT("SimpleQuestEditor", "EntryOnlyActivate", "Quest Start may only connect to the Activate pin"));
        }

        if (IsQuestLikeNode(OutputNode))
        {
            const bool bValidInput = IsQuestLikeNode(InputNode)
                || TraversalPolicy->IsPassThroughNode(InputNode)
                || TraversalPolicy->IsExitNode(InputNode);
            if (!bValidInput)
                return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW,
                    NSLOCTEXT("SimpleQuestEditor", "QuestOutputOnlyToQuest",
                        "Quest outputs may only connect to other Quest nodes or exit nodes"));
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

	// Add Questline Success exit
	{
		TSharedPtr<FEdGraphSchemaAction_NewNode> Action(new FEdGraphSchemaAction_NewNode(
			FText::GetEmpty(),
			NSLOCTEXT("SimpleQuestEditor", "AddExitSuccess", "Add Questline Success"),
			NSLOCTEXT("SimpleQuestEditor", "AddExitSuccessTooltip", "Add a Questline Success exit node"),
			0));
		Action->NodeTemplate = NewObject<UQuestlineNode_Exit_Success>(const_cast<UEdGraph*>(ContextMenuBuilder.CurrentGraph));
		ContextMenuBuilder.AddAction(Action);
	}

	// Add Questline Failure exit
	{
		TSharedPtr<FEdGraphSchemaAction_NewNode> Action(new FEdGraphSchemaAction_NewNode(
			FText::GetEmpty(),
			NSLOCTEXT("SimpleQuestEditor", "AddExitFailure", "Add Questline Failure"),
			NSLOCTEXT("SimpleQuestEditor", "AddExitFailureTooltip", "Add a Questline Failed exit node"),
			0));
		Action->NodeTemplate = NewObject<UQuestlineNode_Exit_Failure>(const_cast<UEdGraph*>(ContextMenuBuilder.CurrentGraph));
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
	        NSLOCTEXT("SimpleQuestEditor", "AddPrereqSetter", "Add Prereq Group"),
	        NSLOCTEXT("SimpleQuestEditor", "AddPrereqSetterTooltip", "Define a named prerequisite group from wired conditions"),
	        0));
	    Action->NodeTemplate = NewObject<UQuestlineNode_PrerequisiteGroupSetter>(const_cast<UEdGraph*>(ContextMenuBuilder.CurrentGraph));
	    ContextMenuBuilder.AddAction(Action);
	}

	// Prereq Group Getter
	{
	    TSharedPtr<FEdGraphSchemaAction_NewNode> Action(new FEdGraphSchemaAction_NewNode(
	        NSLOCTEXT("SimpleQuestEditor", "PrereqCategory", "Prerequisite"),
	        NSLOCTEXT("SimpleQuestEditor", "AddPrereqGetter", "Get Prereq Group"),
	        NSLOCTEXT("SimpleQuestEditor", "AddPrereqGetterTooltip", "Reference a named prerequisite group"),
	        0));
	    Action->NodeTemplate = NewObject<UQuestlineNode_PrerequisiteGroupGetter>(const_cast<UEdGraph*>(ContextMenuBuilder.CurrentGraph));
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



