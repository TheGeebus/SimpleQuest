// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Toolkit/QuestlineGraphPanel.h"
#include "Graph/QuestlineGraphSchema.h"
#include "Nodes/QuestlineNode_Quest.h"
#include "Nodes/QuestlineNode_Knot.h"
#include "Nodes/QuestlineNode_Exit_Success.h"
#include "Nodes/QuestlineNode_Exit_Failure.h"
#include "Nodes/QuestlineNode_Step.h"
#include "Nodes/QuestlineNode_LinkedQuestline.h"
#include "ScopedTransaction.h"
#include "Framework/Application/SlateApplication.h"
#include "GraphEditorDragDropAction.h"

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
        || Key == EKeys::R || Key == EKeys::S || Key == EKeys::F;
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
    if (Key == EKeys::S) return SpawnNode<UQuestlineNode_Exit_Success>(GraphPos);
    if (Key == EKeys::F) return SpawnNode<UQuestlineNode_Exit_Failure>(GraphPos);
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

