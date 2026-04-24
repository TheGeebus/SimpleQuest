// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SWidget.h"

class SWidget;

/**
 * Slate helpers shared across content-node widgets (Step, LinkedQuestline, and any Phase 2 additions for Quest /
 * Exit). Extracted from SGraphNode_QuestlineStep's original private BuildTargetList to allow the same expandable
 * "label + first-item-always-visible + expand-for-the-rest" layout to be used wherever a content-node widget needs
 * to show a list of names (givers, targets, classes, ...). Pure function — no widget state captured; callbacks
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
}

