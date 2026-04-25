// Copyright 2026, Greg Bussell, All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GraphEditor.h"
#include "Widgets/SCompoundWidget.h"
#include "GraphEditor.h"

class UEdGraph;

class SQuestlineGraphPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SQuestlineGraphPanel) {}
		SLATE_ARGUMENT(SGraphEditor::FGraphEditorEvents, GraphEvents)
	SLATE_END_ARGS()

void Construct(const FArguments& InArgs, UEdGraph* InGraph, const TSharedPtr<FUICommandList>& InCommands);
	virtual ~SQuestlineGraphPanel() override;

	TSharedPtr<SGraphEditor> GetGraphEditor() const { return GraphEditor; }

	// SWidget interface
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual bool SupportsKeyboardFocus() const override { return false; }
	virtual FReply OnPreviewKeyDown(const FGeometry&, const FKeyEvent&) override;
	virtual FReply OnKeyDown(const FGeometry&, const FKeyEvent&) override;
	virtual FReply OnKeyUp(const FGeometry&, const FKeyEvent&) override;
	virtual FReply OnPreviewMouseButtonDown(const FGeometry&, const FPointerEvent&) override;

	/** Schedules a JumpToNode after the SGraphEditor's first render pass clears its initial ZoomToFit. */
	void JumpToNodeWhenReady(UEdGraphNode* Node);

	/**
	 * Sets the nodes to draw hover-highlight borders around on this panel's next paint. Replaces the previous set; pass an
	 * empty array to clear. Triggers a paint invalidation so the visual updates on the owning window's next tick — works
	 * across windows and monitors because each editor's panel paints on its own window independently.
	 */
	void SetHoverHighlightedNodes(const TArray<UEdGraphNode*>& Nodes);

	/** Convenience: clears the hover-highlight set. */
	void ClearHoverHighlight();

protected:
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	
private:
	TSet<TWeakObjectPtr<UEdGraphNode>> HoverHighlightedNodes;

	static bool IsHotkey(FKey Key);
	FVector2D ToGraphCoords(const FGeometry& Geometry, FVector2D ScreenPos) const;

	template<typename TNode>
	UEdGraphNode* SpawnNode(FVector2D GraphPos);

	UEdGraphNode* SpawnNodeForKey(FKey Key, FVector2D GraphPos);

	TSharedPtr<SGraphEditor> GraphEditor;

	// Key+click state (no drag)
	FKey HeldHotkey = EKeys::Invalid;

	void HandleFocusChanging(const FFocusEvent& FocusEvent, const FWeakWidgetPath& OldFocusedWidgetPath, const TSharedPtr<SWidget>& OldFocusedWidget,
		const FWidgetPath& NewFocusedWidgetPath, const TSharedPtr<SWidget>& NewFocusedWidget);

	UEdGraphNode* PendingJumpNode = nullptr;
	bool bHasTicked = false;

	/**
	 * Pending pin-alignment entry — captures a freshly-spawned drag-from-pin node and the graph-space position
	 * its connecting pin should land at. Processed in Tick once the node's Slate widget is realized (queried via
	 * SGraphPanel::GetNodeWidgetFromGuid + SGraphNode::FindWidgetForPin). Mirrors JumpToNodeWhenReady's tick-
	 * deferred pattern. Replaces the per-node-type GetPinAlignmentOffset heuristic for the drag-from-pin path
	 * (heuristic remains in place for the key+click no-FromPin path where no specific pin is being aligned).
	 */
	struct FPendingPinAlignment
	{
		TWeakObjectPtr<UEdGraphNode> Node;
		FName ConnectingPinName;
		FVector2D TargetGraphPos = FVector2D::ZeroVector;
		int32 Attempts = 0;
	};
	TArray<FPendingPinAlignment> PendingAlignments;
	void ProcessPendingAlignments();
	void EnqueuePinAlignment(UEdGraphNode* Node, UEdGraphPin* ConnectingPin, FVector2D TargetGraphPos);

	/**
	 * First-stage queue for the right-click action-menu drag-from-pin path. UEdGraph::OnGraphChanged fires Add events
	 * synchronously inside FEdGraphSchemaAction_NewNode::PerformAction — at that moment AutowireNewNode hasn't yet run,
	 * so we can't identify the connecting pin directly. Capture the node + cursor position now, and resolve the connecting
	 * pin at the next Tick (post-PerformAction). The resolved alignment is then handed off to PendingAlignments above.
	 */
	struct FPendingConnectionPinLookup
	{
		TWeakObjectPtr<UEdGraphNode> Node;
		int32 Attempts = 0;
	};
	TArray<FPendingConnectionPinLookup> PendingConnectionLookups;

	TWeakObjectPtr<UEdGraph> SubscribedGraph;
	FDelegateHandle GraphChangedDelegateHandle;

	void OnGraphAddedNodeNotify(const FEdGraphEditAction& EditAction);
	void ProcessPendingConnectionLookups();
};

