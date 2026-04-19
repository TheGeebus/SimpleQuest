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
#include "Styling/SlateStyleRegistry.h"
#include "Utilities/SimpleQuestEditorUtils.h"

void SQuestlineGraphPanel::Construct(const FArguments& InArgs,
                                     UEdGraph* InGraph,
                                     const TSharedPtr<FUICommandList>& InCommands)
{
    SAssignNew(GraphEditor, SGraphEditor)
        .IsEditable(true)
        .GraphToEdit(InGraph)
        .AdditionalCommands(InCommands)
        .GraphEvents(InArgs._GraphEvents);

    ChildSlot[ GraphEditor.ToSharedRef() ];
    FSlateApplication::Get().OnFocusChanging().AddSP(this, &SQuestlineGraphPanel::HandleFocusChanging);
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
}


/*-------------------------------------------*
 *          Helpers
 *-------------------------------------------*/

/**
 * Returns the offset to apply to the raw cursor graph position so the node's primary input pin connector lands at the
 * cursor, matching Blueprint behaviour.
 * - Regular nodes: title bar ~24 + half pin-row ~12 = 36 down, pin nub ~8 right
 * - Knot node: small control point ~16x16, so centre it on the cursor
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

    const FVector2D GraphPos = ToGraphCoords(Geometry, FSlateApplication::Get().GetCursorPos()) + GetPinAlignmentOffset(KeyEvent.GetKey());

    const FScopedTransaction Transaction(NSLOCTEXT("SimpleQuestEditor", "HotkeyPlaceFromDrag", "Place Node (Hotkey)"));

    UEdGraphNode* NewNode = SpawnNodeForKey(KeyEvent.GetKey(), GraphPos);
    if (!NewNode) return FReply::Unhandled();

    if (FromPin)
    {
        NewNode->AutowireNewNode(FromPin);
        FromPin->GetOwningNode()->NodeConnectionListChanged();
    }

    UEdGraph* Graph = NewNode->GetGraph();
    Graph->NotifyGraphChanged();
    if (UObject* Outer = Graph->GetOuter())
    {
        Outer->PostEditChange();
        Outer->MarkPackageDirty();
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

    if (HoverHighlightedNodes.Num() == 0 || !GraphEditor.IsValid())
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

    for (const TWeakObjectPtr<UEdGraphNode>& WeakNode : HoverHighlightedNodes)
    {
        UEdGraphNode* Node = WeakNode.Get();
        if (!Node) continue;

        TSharedPtr<SGraphNode> NodeWidget = Panel->GetNodeWidgetFromGuid(Node->NodeGuid);
        if (!NodeWidget.IsValid()) continue;

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

    return HighlightLayer;
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

