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

};
