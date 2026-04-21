// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "SGraphNode.h"

class UQuestlineNode_LinkedQuestline;
class SVerticalBox;
struct FAssetData;

/**
 * Slate widget for LinkedQuestline nodes. Inlines a UQuestlineGraph asset picker on the node body so designers can
 * assign (or reassign) the referenced questline without opening Details. OnObjectChanged drives a scoped transaction,
 * LinkedGraph assignment, RebuildOutcomePinsFromLinkedGraph, and NotifyGraphChanged.
 *
 * Visual signal: when LinkedGraph is null, the node paints a red shadow outline (same affordance Step uses for a
 * missing ObjectiveClass). The node title ("Linked Questline - <name>") re-queries on graph-changed, so FriendlyName
 * flows through immediately after asset selection.
 */
class SGraphNode_LinkedQuestline : public SGraphNode
{
public:
	SLATE_BEGIN_ARGS(SGraphNode_LinkedQuestline) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UQuestlineNode_LinkedQuestline* InNode);

	virtual void UpdateGraphNode() override;

private:
	TSharedRef<SWidget> CreateAssetPickerWidget();

	FString GetAssetPath() const;
	void OnAssetChanged(const FAssetData& NewAsset);
	bool OnShouldFilterAsset(const FAssetData& AssetData) const;

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	UQuestlineNode_LinkedQuestline* LinkedNode = nullptr;
};