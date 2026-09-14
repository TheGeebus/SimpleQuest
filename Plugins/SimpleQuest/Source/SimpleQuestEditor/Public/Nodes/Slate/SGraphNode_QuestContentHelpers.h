// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SWidget.h"

class SWidget;
class SNodeTitle;
class UEdGraphNode;

/**
 * Slate helpers shared across content-node widgets (Step, LinkedQuestline, and any Phase 2 additions for Quest /
 * Exit). Extracted from SGraphNode_QuestlineStep's original private BuildTargetList to allow the same expandable
 * "label + first-item-always-visible + expand-for-the-rest" layout to be used wherever a content-node widget needs
 * to show a list of names (givers, targets, classes, ...). Pure function - no widget state captured; callbacks
 * read back state from whatever the caller keeps authoritative.
 */
namespace FQuestNodeSlateHelpers
{
	/**
	 * Build a row showing {Label}: {Items[0]}, with a chevron button on the left that expands to show Items[1..N].
	 * Returns SNullWidget for empty input. Label and items are tinted with Color. IsExpanded / ToggleExpanded
	 * own the collapse state (typically backed by a UPROPERTY on the node instance so the state persists across
	 * widget rebuilds and saves with the asset).
	 */
	TSharedRef<SWidget> BuildLabeledExpandableList(const FText& Label, const TArray<FString>& Items, const FLinearColor& Color,	TFunction<bool()> IsExpanded, TFunction<void()> ToggleExpanded);

	/**
	 * Build the yellow "Recompile to update tags" warning bar used by every content-node widget to surface a stale
	 * tag state after a rename. Visibility tracks IsVisible, so callers keep authoritative stale-state storage
	 * (typically UQuestlineNode_ContentBase::bTagStale) and the widget only renders when the caller says so.
	 */
	TSharedRef<SWidget> BuildStaleTagWarningBar(TAttribute<bool> IsVisible);

	/**
	 * Commit a data edit to a graph node on the NEXT TICK, inside a transaction, then refresh only that node's
	 * title. Use this from ANY inline node-widget callback that writes to the node - tag pickers, class pickers,
	 * asset pickers, toggles. Three separate hazards it exists to avoid, each of which cost an editor crash or an
	 * ensure to find:
	 *
	 * 1. DO NOT EDIT SYNCHRONOUSLY FROM A WIDGET CALLBACK. Inline pickers hand control back while a Slate input
	 *    event is still running. On a RIGHT-click, STableRow::OnMouseButtonUp selects the row - which is what
	 *    reaches the callback - and THEN pushes a context menu parented to the picker's still-open dropdown
	 *    window. Anything synchronous that rebuilds widgets destroys that window first, and the engine's push
	 *    asserts on a dead parent HWND: "Window Creation Failed (Error Code 1400)". Left-click never shows it,
	 *    because nothing is pushed after a left-click.
	 *
	 * 2. THE TIMER MUST NOT BELONG TO THE CALLING WIDGET. A widget-owned active timer can be torn down by the
	 *    very rebuild its callback triggers, stranding an SGraphPin hard ref that the next paint ensures on
	 *    (!bGraphDataInvalid, SGraphPin::GetPinObj). GEditor's timer manager outlives the node widget.
	 *
	 * 3. REFRESH THE TITLE, NOT THE GRAPH. UEdGraph::NotifyGraphChanged makes SGraphPanel rebuild the node, which
	 *    slams an open picker dropdown shut the instant a value is chosen - the Details-panel picker stays open
	 *    because nothing rebuilds it. SNodeTitle::MarkDirty re-queries GetNodeTitle without touching widgets.
	 *
	 * Apply runs only if Node survives the tick. Capture everything else it needs WEAKLY - a frame has passed.
	 */
	void CommitNodeEditDeferred(UEdGraphNode* Node, const FText& TransactionText, TFunction<void()> Apply);
}

