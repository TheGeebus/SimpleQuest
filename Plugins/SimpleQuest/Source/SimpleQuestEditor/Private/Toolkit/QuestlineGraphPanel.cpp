// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Toolkit/QuestlineGraphPanel.h"
#include "Graph/QuestlineGraphSchema.h"
#include "Nodes/QuestlineNode_Quest.h"
#include "Nodes/QuestlineNode_Knot.h"
#include "Nodes/QuestlineNode_Exit.h"
#include "Nodes/QuestlineNode_Step.h"
#include "Nodes/QuestlineNode_LinkedQuestline.h"
#include "ScopedTransaction.h"
#include "Framework/Application/SlateApplication.h"
#include "GraphEditorDragDropAction.h"
#include "SGraphPanel.h"
#include "SimpleQuestEditor.h"
#include "SimpleQuestLog.h"
#include "Debug/QuestNodeDebugState.h"
#include "Debug/QuestPIEDebugChannel.h"
#include "Styling/SlateStyleRegistry.h"
#include "Utilities/SimpleQuestEditorUtils.h"


namespace PIEOverlay_Style
{
    // sRGB-correct per-state tints. FColor → FLinearColor constructor does sRGB→linear conversion so authored values match
    // their on-screen appearance. Priority order matches FQuestPIEDebugChannel::QueryNodeState's selection logic.
    static const FLinearColor Blocked      = FLinearColor(FColor(230,  60,  60));  // red — highest urgency
    static const FLinearColor PendingGiver = FLinearColor(FColor( 80, 180, 230));  // cyan — waiting
    static const FLinearColor Active       = FLinearColor(FColor(250, 200,  60));  // amber — running
    static const FLinearColor Completed    = FLinearColor(FColor( 90, 210, 110));  // green — done
    static const FLinearColor Deactivated  = FLinearColor(FColor(150, 150, 150));  // grey — inert
    static const FLinearColor DebugBadge   = FLinearColor(FColor(250, 200,  60));  // badge text color when overlay active

    const FLinearColor& ColorForState(EQuestNodeDebugState State)
    {
        switch (State)
        {
        case EQuestNodeDebugState::Blocked:       return Blocked;
        case EQuestNodeDebugState::PendingGiver:  return PendingGiver;
        case EQuestNodeDebugState::Active:        return Active;
        case EQuestNodeDebugState::Completed:     return Completed;
        case EQuestNodeDebugState::Deactivated:   return Deactivated;
        default:                                  return Deactivated; // unused — Unknown returns early before this is called
        }
    }
}

void SQuestlineGraphPanel::Construct(const FArguments& InArgs, UEdGraph* InGraph, const TSharedPtr<FUICommandList>& InCommands)
{
    SAssignNew(GraphEditor, SGraphEditor)
        .IsEditable(true)
        .GraphToEdit(InGraph)
        .AdditionalCommands(InCommands)
        .GraphEvents(InArgs._GraphEvents);

    ChildSlot[ GraphEditor.ToSharedRef() ];
    FSlateApplication::Get().OnFocusChanging().AddSP(this, &SQuestlineGraphPanel::HandleFocusChanging);

    // Subscribe to OnGraphChanged so the right-click action-menu drag-from-pin path also benefits from pin-precise
    // alignment, not just the QWERX hotkey path. Two-stage flow: this handler captures (Node, CursorPos) at AddNode
    // time; the next Tick resolves the autowired pin and hands off to the existing alignment queue. Handle stored so
    // we can unsubscribe in the destructor.
    if (InGraph)
    {
        SubscribedGraph = InGraph;
        GraphChangedDelegateHandle = InGraph->AddOnGraphChangedHandler(FOnGraphChanged::FDelegate::CreateSP(this, &SQuestlineGraphPanel::OnGraphAddedNodeNotify));
    }
}

SQuestlineGraphPanel::~SQuestlineGraphPanel()
{
    if (GraphChangedDelegateHandle.IsValid())
    {
        if (UEdGraph* Graph = SubscribedGraph.Get())
        {
            Graph->RemoveOnGraphChangedHandler(GraphChangedDelegateHandle);
        }
    }
}

void SQuestlineGraphPanel::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
    SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
    if (PendingJumpNode && bHasTicked)
    {
        GraphEditor->JumpToNode(PendingJumpNode, false, true);
        PendingJumpNode = nullptr;
    }
    bHasTicked = true;

    // Two-stage drain. PendingConnectionLookups (action-menu path) resolves the autowired pin first, then enqueues
    // into PendingAlignments. PendingAlignments (both paths converge here) applies the position correction once
    // the node's Slate widget is realized. Order matters — process lookups first so any newly-resolved entries can
    // also drain into PendingAlignments in the same tick if widgets happen to be ready already.
    if (!PendingConnectionLookups.IsEmpty()) ProcessPendingConnectionLookups();
    if (!PendingAlignments.IsEmpty()) ProcessPendingAlignments();

    // Poll paint invalidation while the PIE debug channel is active so the overlay stays live as WorldState facts change
    // mid-game. Low cost — Invalidate(Paint) coalesces with Slate's per-frame paint pass; OnPaint's debug-overlay block
    // only does real work when IsActive() is true, so the cost outside PIE is zero.
    if (FQuestPIEDebugChannel* Channel = FSimpleQuestEditor::GetPIEDebugChannel())
    {
        if (Channel->IsActive())
        {
            Invalidate(EInvalidateWidget::Paint);
        }
    }
}


/*-------------------------------------------*
 *          Helpers
 *-------------------------------------------*/

/**
 * Coarse fallback offset for the no-FromPin placement path (key+click without an active drag-from-pin). Matches Blueprint's
 * "primary input pin under cursor" behaviour as a default visual alignment.
 *   - Regular nodes: title bar ~24 + half pin-row ~12 = 36 down, pin nub ~8 right
 *   - Knot node: small control point ~16x16, so centre it on the cursor
 *
 * The drag-from-pin path uses pin-precise alignment instead (see EnqueuePinAlignment / ProcessPendingAlignments) so the
 * specific connecting pin lands exactly at cursor regardless of node type, content size, or which pin connects.
 */
static FVector2D GetPinAlignmentOffset(FKey Key)
{
    if (Key == EKeys::R) return FVector2D(-8.f, -8.f);
    return FVector2D(-8.f, -36.f);
}

bool SQuestlineGraphPanel::IsHotkey(FKey Key)
{
    return Key == EKeys::Q || Key == EKeys::W || Key == EKeys::E
        || Key == EKeys::R || Key == EKeys::X;
}

FVector2D SQuestlineGraphPanel::ToGraphCoords(const FGeometry& Geometry, FVector2D ScreenPos) const
{
    FVector2f ViewLocation;
    float ZoomAmount;
    GraphEditor->GetViewLocation(ViewLocation, ZoomAmount);
    const FVector2f LocalPos = Geometry.AbsoluteToLocal(ScreenPos);
    return FVector2D(ViewLocation + LocalPos / ZoomAmount);    
}

template<typename TNode>
UEdGraphNode* SQuestlineGraphPanel::SpawnNode(FVector2D GraphPos)
{
    UEdGraph* Graph = GraphEditor->GetCurrentGraph();
    FGraphNodeCreator<TNode> Creator(*Graph);
    UEdGraphNode* Node = Creator.CreateNode();
    Node->NodePosX = FMath::RoundToInt(GraphPos.X);
    Node->NodePosY = FMath::RoundToInt(GraphPos.Y);
    Creator.Finalize();
    return Node;
}

UEdGraphNode* SQuestlineGraphPanel::SpawnNodeForKey(FKey Key, FVector2D GraphPos)
{
    UEdGraph* Graph = GraphEditor->GetCurrentGraph();
    if (!Graph) return nullptr;

    Graph->Modify();
    if (Key == EKeys::Q) return SpawnNode<UQuestlineNode_Quest>(GraphPos);
    if (Key == EKeys::W) return SpawnNode<UQuestlineNode_Step>(GraphPos);
    if (Key == EKeys::E) return SpawnNode<UQuestlineNode_LinkedQuestline>(GraphPos);
    if (Key == EKeys::R) return SpawnNode<UQuestlineNode_Knot>(GraphPos);
    if (Key == EKeys::X) return SpawnNode<UQuestlineNode_Exit>(GraphPos);
    return nullptr;
}

/*-------------------------------------------*
 *          Key Events
 *-------------------------------------------*/

FReply SQuestlineGraphPanel::OnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent)
{
    if (!IsHotkey(KeyEvent.GetKey())) return FReply::Unhandled();

    TSharedPtr<FDragDropOperation> DragOp = FSlateApplication::Get().GetDragDroppingContent();
    if (!DragOp.IsValid() || !DragOp->IsOfType<FGraphEditorDragDropAction>()) return FReply::Unhandled();

    UEdGraphPin* FromPin = UQuestlineGraphSchema::GetActiveDragFromPin();
    UQuestlineGraphSchema::ClearActiveDragFromPin();

    FSlateApplication::Get().CancelDragDrop();

    // Cursor position in graph coords — this is where we want the connecting pin to land (drag-from-pin path) or
    // where the node's primary pin should land (no-FromPin fallback path, via the heuristic offset).
    const FVector2D CursorGraphPos = ToGraphCoords(Geometry, FSlateApplication::Get().GetCursorPos());
    const FVector2D SpawnGraphPos = FromPin
        ? CursorGraphPos                                          // pin-precise path: spawn at cursor; correct in Tick
        : CursorGraphPos + GetPinAlignmentOffset(KeyEvent.GetKey()); // fallback path: heuristic offset (no FromPin)

    const FScopedTransaction Transaction(NSLOCTEXT("SimpleQuestEditor", "HotkeyPlaceFromDrag", "Place Node (Hotkey)"));

    UEdGraphNode* NewNode = SpawnNodeForKey(KeyEvent.GetKey(), SpawnGraphPos);
    if (!NewNode) return FReply::Unhandled();

    UEdGraphPin* ConnectedPin = nullptr;
    if (FromPin)
    {
        // Snapshot the pin link state before AutowireNewNode so we can identify which pin received the connection.
        TSet<UEdGraphPin*> PreLinkedPins;
        for (UEdGraphPin* Pin : NewNode->Pins) { if (Pin) PreLinkedPins.Add(Pin); }

        NewNode->AutowireNewNode(FromPin);
        FromPin->GetOwningNode()->NodeConnectionListChanged();

        // Find the pin on NewNode that's now linked to FromPin — AutowireNewNode either connected one pin or none.
        for (UEdGraphPin* Pin : NewNode->Pins)
        {
            if (Pin && Pin->LinkedTo.Contains(FromPin))
            {
                ConnectedPin = Pin;
                break;
            }
        }
    }

    UEdGraph* Graph = NewNode->GetGraph();
    Graph->NotifyGraphChanged();
    if (UObject* Outer = Graph->GetOuter())
    {
        Outer->PostEditChange();
        Outer->MarkPackageDirty();
    }

    // Pin-precise alignment: enqueue a deferred correction so the connecting pin lands exactly at cursor once the
    // node's Slate widget is realized (next tick). Falls through silently for the no-FromPin path or any case where
    // AutowireNewNode didn't actually connect (Candidate walk found no acceptable target).
    if (FromPin && ConnectedPin)
    {
        EnqueuePinAlignment(NewNode, ConnectedPin, CursorGraphPos);
    }

    return FReply::Handled();
}

FReply SQuestlineGraphPanel::OnKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent)
{
    // Record hotkey for the key+click (no-drag) path
    if (IsHotkey(KeyEvent.GetKey())) HeldHotkey = KeyEvent.GetKey();
    return FReply::Unhandled();
}

FReply SQuestlineGraphPanel::OnKeyUp(const FGeometry& Geometry, const FKeyEvent& KeyEvent)
{
    if (KeyEvent.GetKey() == HeldHotkey) HeldHotkey = EKeys::Invalid;
    return FReply::Unhandled();
}

void SQuestlineGraphPanel::HandleFocusChanging(const FFocusEvent& FocusEvent, const FWeakWidgetPath& OldFocusedWidgetPath,
    const TSharedPtr<SWidget>& OldFocusedWidget, const FWidgetPath& NewFocusedWidgetPath, const TSharedPtr<SWidget>& NewFocusedWidget)
{
    // If focus has moved somewhere outside our subtree, any held hotkey is now stale
    if (HeldHotkey != EKeys::Invalid && !NewFocusedWidgetPath.ContainsWidget(this)) HeldHotkey = EKeys::Invalid;
}

void SQuestlineGraphPanel::JumpToNodeWhenReady(UEdGraphNode* Node)
{
    if (!Node || !GraphEditor.IsValid()) return;
    if (bHasTicked)
        GraphEditor->JumpToNode(Node, false, true);
    else
        PendingJumpNode = Node;
}

/*-------------------------------------------*
 * Examiner Hover Behaviors
 *-------------------------------------------*/

void SQuestlineGraphPanel::SetHoverHighlightedNodes(const TArray<UEdGraphNode*>& Nodes)
{
	TSet<TWeakObjectPtr<UEdGraphNode>> NewSet;
	for (UEdGraphNode* Node : Nodes)
	{
		if (Node) NewSet.Add(Node);
	}
	if (NewSet.Num() != HoverHighlightedNodes.Num() || !NewSet.Includes(HoverHighlightedNodes))
	{
		HoverHighlightedNodes = MoveTemp(NewSet);
		Invalidate(EInvalidateWidget::Paint);
	}
}

void SQuestlineGraphPanel::ClearHoverHighlight()
{
	if (HoverHighlightedNodes.Num() > 0)
	{
		HoverHighlightedNodes.Empty();
		Invalidate(EInvalidateWidget::Paint);
	}
}

int32 SQuestlineGraphPanel::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
    // Paint children first so each node widget's cached paint geometry is fresh when we query it below.
    const int32 ChildLayer = SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);

    if (!GraphEditor.IsValid())
    {
        return ChildLayer;
    }

    SGraphPanel* Panel = GraphEditor->GetGraphPanel();
    if (!Panel)
    {
        return ChildLayer;
    }

    const FLinearColor HighlightColor = SQ_ED_HOVER_HIGHLIGHT;
    // Twice the selected-shadow inflation so the hover halo reads louder than a plain selection.
    const FVector2f ShadowInflate = UE::Slate::CastToVector2f(GetDefault<UGraphEditorSettings>()->GetShadowDeltaSize()) * 2.f;
    const int32 HighlightLayer = ChildLayer + 1;

    // Graph-space viewport rect — derived from the panel's view offset, size, and zoom. Used to cull halos for nodes
    // that are currently off-screen in graph coordinates, independent of their (possibly stale) paint-space geometry.
    // SGraphPanel applies zoom as a render transform, so SGraphNode::GetDesiredSize() is 1:1 with graph units.
    const float Zoom = FMath::Max(Panel->GetZoomAmount(), KINDA_SMALL_NUMBER);
    const FVector2D ViewOffset = Panel->GetViewOffset();
    const FVector2D PanelLocalSize = Panel->GetTickSpaceGeometry().GetLocalSize();
    const FSlateRect ViewGraphRect(
        ViewOffset.X,
        ViewOffset.Y,
        ViewOffset.X + PanelLocalSize.X / Zoom,
        ViewOffset.Y + PanelLocalSize.Y / Zoom);

    for (const TWeakObjectPtr<UEdGraphNode>& WeakNode : HoverHighlightedNodes)
    {
        UEdGraphNode* Node = WeakNode.Get();
        if (!Node) continue;

        TSharedPtr<SGraphNode> NodeWidget = Panel->GetNodeWidgetFromGuid(Node->NodeGuid);
        if (!NodeWidget.IsValid()) continue;

        // Cull in graph space — skip nodes outside the current viewport. Paint-space geometry goes stale for culled
        // nodes (SGraphPanel stops painting them, so their cached geometry stays at the last visible position near an
        // edge). Testing the authoritative NodePosX/Y against ViewGraphRect avoids relying on the stale cache.
        const FVector2D NodeGraphPos(Node->NodePosX, Node->NodePosY);
        const FVector2D NodeGraphSize = NodeWidget->GetDesiredSize();
        const FSlateRect NodeGraphRect(
            NodeGraphPos.X,
            NodeGraphPos.Y,
            NodeGraphPos.X + NodeGraphSize.X,
            NodeGraphPos.Y + NodeGraphSize.Y);
        if (!FSlateRect::DoRectanglesIntersect(NodeGraphRect, ViewGraphRect)) continue;

        // For on-screen nodes, paint-space is fresh (children were just painted by the SCompoundWidget::OnPaint call
        // at the top of this method) — use it to position the halo exactly where the node was drawn.
        const FGeometry& NodeGeom = NodeWidget->GetPaintSpaceGeometry();
        if (NodeGeom.GetLocalSize().IsNearlyZero()) continue;

        static const ISlateStyle* SimpleQuestStyle = FSlateStyleRegistry::FindSlateStyle("SimpleQuestStyle");
        const FSlateBrush* HoverHaloBrush = SimpleQuestStyle
            ? SimpleQuestStyle->GetBrush("SimpleQuest.Graph.Node.HoverHalo")
            : FAppStyle::GetBrush("Graph.Node.ShadowSelected");  // fallback if style missing

        FSlateDrawElement::MakeBox(
            OutDrawElements,
            HighlightLayer,
            NodeGeom.ToInflatedPaintGeometry(ShadowInflate),
            HoverHaloBrush,
            ESlateDrawEffect::None,
            HighlightColor
        );
    }

    // ---- PIE debug overlay pass ------------------------------------------------------------------------------------
    // Runs only when PIE is active and the channel has resolved subsystems. Iterates the same viewport-culled node set
    // as the hover halo above and paints a state-colored border halo per content node. Layered above the hover halo so
    // a hovered node that's also in a state shows both (hover reads as saturation; state reads as the color).
    int32 TopLayer = HighlightLayer;
    if (FQuestPIEDebugChannel* DebugChannel = FSimpleQuestEditor::GetPIEDebugChannel())
    {
        if (DebugChannel->IsActive())
        {
            const int32 DebugOverlayLayer = HighlightLayer + 1;
            static const ISlateStyle* SimpleQuestStyle = FSlateStyleRegistry::FindSlateStyle("SimpleQuestStyle");
            const FSlateBrush* DebugBrush = SimpleQuestStyle
                ? SimpleQuestStyle->GetBrush("SimpleQuest.Graph.Node.HoverHalo")
                : FAppStyle::GetBrush("Graph.Node.ShadowSelected");

            for (UEdGraphNode* Node : Panel->GetGraphObj()->Nodes)
            {
                if (!Node) continue;

                const EQuestNodeDebugState State = DebugChannel->QueryNodeState(Node);
                if (State == EQuestNodeDebugState::Unknown) continue;

                TSharedPtr<SGraphNode> NodeWidget = Panel->GetNodeWidgetFromGuid(Node->NodeGuid);
                if (!NodeWidget.IsValid()) continue;

                const FVector2D NodeGraphPos(Node->NodePosX, Node->NodePosY);
                const FVector2D NodeGraphSize = NodeWidget->GetDesiredSize();
                const FSlateRect NodeGraphRect(NodeGraphPos.X, NodeGraphPos.Y, NodeGraphPos.X + NodeGraphSize.X, NodeGraphPos.Y + NodeGraphSize.Y);
                if (!FSlateRect::DoRectanglesIntersect(NodeGraphRect, ViewGraphRect)) continue;

                const FGeometry& NodeGeom = NodeWidget->GetPaintSpaceGeometry();
                if (NodeGeom.GetLocalSize().IsNearlyZero()) continue;

                FSlateDrawElement::MakeBox(
                    OutDrawElements,
                    DebugOverlayLayer,
                    NodeGeom.ToInflatedPaintGeometry(ShadowInflate),
                    DebugBrush,
                    ESlateDrawEffect::None,
                    PIEOverlay_Style::ColorForState(State)
                );
            }
            TopLayer = DebugOverlayLayer;

            // ---- "DEBUG (PIE)" badge in the panel's top-left corner ---------------------------------------------------
            const FSlateFontInfo BadgeFont = FCoreStyle::GetDefaultFontStyle("Bold", 10);
            const FVector2D BadgePos(12.f, 8.f);
            FSlateDrawElement::MakeText(
                OutDrawElements,
                DebugOverlayLayer + 1,
                AllottedGeometry.ToOffsetPaintGeometry(BadgePos),
                FString(TEXT("DEBUG (PIE)")),
                BadgeFont,
                ESlateDrawEffect::None,
                PIEOverlay_Style::DebugBadge
            );
            TopLayer = DebugOverlayLayer + 1;
        }
    }

    return TopLayer;
}


/*-------------------------------------------*
 *          Mouse Events
 *-------------------------------------------*/

FReply SQuestlineGraphPanel::OnPreviewMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& MouseEvent)
{
    if (HeldHotkey == EKeys::Invalid) return FReply::Unhandled();
    if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton) return FReply::Unhandled();

    const FVector2D GraphPos = ToGraphCoords(Geometry, MouseEvent.GetScreenSpacePosition()) + GetPinAlignmentOffset(HeldHotkey);
    
    const FScopedTransaction Transaction(NSLOCTEXT("SimpleQuestEditor", "HotkeyPlaceNode", "Place Node (Hotkey)"));

    SpawnNodeForKey(HeldHotkey, GraphPos);

    UEdGraph* Graph = GraphEditor->GetCurrentGraph();
    if (Graph)
    {
        Graph->NotifyGraphChanged();
        if (UObject* Outer = Graph->GetOuter())
        {
            Outer->PostEditChange();
            Outer->MarkPackageDirty();
        }
    }
    return FReply::Handled();
}

/*-------------------------------------------*
 *          Pin-precise alignment
 *-------------------------------------------*/

void SQuestlineGraphPanel::EnqueuePinAlignment(UEdGraphNode* Node, UEdGraphPin* ConnectingPin, FVector2D TargetGraphPos)
{
    if (!Node || !ConnectingPin) return;
    FPendingPinAlignment Entry;
    Entry.Node = Node;
    Entry.ConnectingPinName = ConnectingPin->PinName;
    Entry.TargetGraphPos = TargetGraphPos;
    PendingAlignments.Add(MoveTemp(Entry));
}

void SQuestlineGraphPanel::ProcessPendingAlignments()
{
    if (!GraphEditor.IsValid()) { PendingAlignments.Empty(); return; }
    SGraphPanel* Panel = GraphEditor->GetGraphPanel();
    if (!Panel) { PendingAlignments.Empty(); return; }

    // Cap retries — if the widget isn't realized after this many ticks the spawn likely raced something else
    // (asset reload, editor close, etc.). Discard rather than hold forever.
    static constexpr int32 MaxAttempts = 5;

    for (int32 Idx = PendingAlignments.Num() - 1; Idx >= 0; --Idx)
    {
        FPendingPinAlignment& Entry = PendingAlignments[Idx];
        UEdGraphNode* Node = Entry.Node.Get();
        if (!Node) { PendingAlignments.RemoveAt(Idx); continue; }

        TSharedPtr<SGraphNode> NodeWidget = Panel->GetNodeWidgetFromGuid(Node->NodeGuid);
        const bool bWidgetReady = NodeWidget.IsValid() && !NodeWidget->GetDesiredSize().IsNearlyZero();
        if (!bWidgetReady)
        {
            if (++Entry.Attempts >= MaxAttempts) PendingAlignments.RemoveAt(Idx);
            continue;
        }

        UEdGraphPin* Pin = Node->FindPin(Entry.ConnectingPinName);
        TSharedPtr<SGraphPin> PinWidget = Pin ? NodeWidget->FindWidgetForPin(Pin) : nullptr;
        if (!PinWidget.IsValid() || PinWidget->GetTickSpaceGeometry().GetLocalSize().IsNearlyZero())
        {
            if (++Entry.Attempts >= MaxAttempts) PendingAlignments.RemoveAt(Idx);
            continue;
        }

        // Resolve the pin connector's current graph-space center, then shift the node by the delta so the pin lands
        // at TargetGraphPos. The pin connector is the small circular nub at the outer edge of the pin widget — left
        // edge for input pins, right edge for output pins — NOT the center of the whole pin+label widget. Aligning
        // to the widget center would offset by half the label width, which is the bug we're correcting from the
        // initial implementation.
        // Path: connector local pos → absolute pixels → panel-local → graph coords (via SNodePanel::PanelCoordToGraphCoord,
        // which folds in zoom + view offset). Avoids manual zoom math; matches the panel's own coordinate machinery.
        const FGeometry& PinGeom = PinWidget->GetTickSpaceGeometry();
        const FVector2D PinLocalSize = PinGeom.GetLocalSize();
        // FAppStyle's "Graph.Pin.Connected" / "Graph.Pin.Disconnected" brushes are 11×11 by default, so the
        // connector glyph's center sits ~5.5px in from the relevant edge. Hardcoded rather than queried because
        // the brush is style-managed and we don't have a SGraphPin API for "give me the connector center" exposed.
        static constexpr float ConnectorInset = 5.5f;
        const float ConnectorX = (Pin->Direction == EGPD_Input)
            ? ConnectorInset
            : PinLocalSize.X - ConnectorInset;
        const FVector2D PinConnectorLocal(ConnectorX, PinLocalSize.Y * 0.5f);
        const FVector2D PinAbsCenter = PinGeom.LocalToAbsolute(PinConnectorLocal);
        const FVector2D PinPanelLocal = Panel->GetTickSpaceGeometry().AbsoluteToLocal(PinAbsCenter);
        const FVector2D PinGraphPos = Panel->PanelCoordToGraphCoord(PinPanelLocal);

        const FVector2D Delta = Entry.TargetGraphPos - PinGraphPos;

        Node->Modify();
        Node->NodePosX += FMath::RoundToInt(Delta.X);
        Node->NodePosY += FMath::RoundToInt(Delta.Y);

        if (UEdGraph* Graph = Node->GetGraph())
        {
            Graph->NotifyGraphChanged();
        }

        UE_LOG(LogSimpleQuest, VeryVerbose, TEXT("PinAlignment: '%s' pin '%s' shifted by (%.1f, %.1f) → final pos (%d, %d)"),
            *Node->GetName(), *Entry.ConnectingPinName.ToString(), Delta.X, Delta.Y, Node->NodePosX, Node->NodePosY);

        PendingAlignments.RemoveAt(Idx);
    }
}

void SQuestlineGraphPanel::OnGraphAddedNodeNotify(const FEdGraphEditAction& EditAction)
{
    if (!(EditAction.Action & GRAPHACTION_AddNode)) return;

    // Drag-from-pin signal — set every frame by DrawPreviewConnector while the user drags, persists through drop.
    // Without this filter, programmatic AddNode calls (paste, schema-driven knot insertion in self-loops, etc.)
    // would be misclassified as drag-from-pin spawns. Cleared at the end of this handler so the next drag is
    // distinguishable from the previous one.
    UEdGraphPin* ActiveDragPin = UQuestlineGraphSchema::GetActiveDragFromPin();
    if (!ActiveDragPin) return;

    // Note: we do NOT capture cursor position here. By the time PerformAction completes (next Tick), the new node's
    // NodePosX/Y holds the drop location — same value passed as `Location` to FEdGraphSchemaAction_NewNode::Perform-
    // Action, which is where the wire-drag was released and the action menu opened. That's the anchor point we want.
    // Capturing cursor here would instead snapshot wherever the user clicked the menu *item*, which is unrelated to
    // the drop point and shifts every time the user picks a different menu position.

    for (const UEdGraphNode* AddedNode : EditAction.Nodes)
    {
        if (!AddedNode) continue;
        // Skip knots — UQuestlineGraphSchema::TryCreateConnection adds them at specific arch coordinates as part
        // of self-loop handling. Aligning their connector to cursor would override the schema's deliberate placement.
        // The action-menu "Add Reroute Node" path also creates knots, but for that case the default top-left-at-cursor
        // is close enough given the knot's small symmetric widget (16×16); pin-precise alignment isn't needed.
        if (AddedNode->IsA<UQuestlineNode_Knot>()) continue;

        FPendingConnectionPinLookup Entry;
        Entry.Node = const_cast<UEdGraphNode*>(AddedNode);
        PendingConnectionLookups.Add(MoveTemp(Entry));
    }

    // Consumed — reset so subsequent unrelated AddNode events don't inherit this drag's signal.
    UQuestlineGraphSchema::ClearActiveDragFromPin();
}

void SQuestlineGraphPanel::ProcessPendingConnectionLookups()
{
    // AutowireNewNode runs synchronously inside FEdGraphSchemaAction_NewNode::PerformAction, so by the time this
    // method runs at the next Tick, the connection either exists or never will. One attempt is enough; no retries.
    static constexpr int32 MaxAttempts = 1;

    for (int32 Idx = PendingConnectionLookups.Num() - 1; Idx >= 0; --Idx)
    {
        FPendingConnectionPinLookup& Entry = PendingConnectionLookups[Idx];
        UEdGraphNode* Node = Entry.Node.Get();
        if (!Node) { PendingConnectionLookups.RemoveAt(Idx); continue; }

        // Find the pin that AutowireNewNode connected — the one with a link to a pin on a different node.
        UEdGraphPin* ConnectingPin = nullptr;
        for (UEdGraphPin* Pin : Node->Pins)
        {
            if (!Pin) continue;
            for (UEdGraphPin* Linked : Pin->LinkedTo)
            {
                if (Linked && Linked->GetOwningNode() != Node)
                {
                    ConnectingPin = Pin;
                    break;
                }
            }
            if (ConnectingPin) break;
        }

        if (!ConnectingPin)
        {
            // AutowireNewNode found no acceptable candidate (rare — typically means the from-pin's category and
            // the new node's pins don't have any compatible pairing). Discard rather than retry; the connection
            // won't materialize spontaneously. Node stays at its initial drop position (existing behavior).
            if (++Entry.Attempts >= MaxAttempts) PendingConnectionLookups.RemoveAt(Idx);
            continue;
        }

        // Use the node's current NodePos as the alignment target. By the time PerformAction has completed (this
        // Tick runs after the action's synchronous run wraps), NodePosX/Y is the drop position the user released
        // the wire over — same value passed as `Location` to FEdGraphSchemaAction_NewNode::PerformAction. That's
        // "where we stopped dragging the wire", which is what designers expect the connector to land on, NOT the
        // current cursor position (which has moved to wherever the menu item was clicked).
        const FVector2D DropGraphPos(Node->NodePosX, Node->NodePosY);
        EnqueuePinAlignment(Node, ConnectingPin, DropGraphPos);
        PendingConnectionLookups.RemoveAt(Idx);
    }
}

