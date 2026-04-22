// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "SGraphNode.h"

class UQuestlineNode_Step;
class SVerticalBox;

class SGraphNode_QuestlineStep : public SGraphNode
{
public:
	SLATE_BEGIN_ARGS(SGraphNode_QuestlineStep) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UQuestlineNode_Step* InNode);

	// SGraphNode overrides
	virtual void UpdateGraphNode() override;

private:
	// Body content builders
	TSharedRef<SWidget> CreateObjectiveInfoWidget();
	TSharedRef<SWidget> CreateTargetSummaryWidget();
	TSharedRef<SWidget> CreateExpandedContentWidget();

	// Expand / collapse
	void CreateExpandCollapseArrow(TSharedPtr<SVerticalBox> MainBox);
	void OnExpandCollapseChanged(const ECheckBoxState NewState);
	ECheckBoxState GetExpandCollapseState() const;
	const FSlateBrush* GetExpandCollapseArrow() const;
	EVisibility GetExpandedContentVisibility() const;

	// Red border — drawn as an inflated shadow in OnPaint
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	// Objective class picker
	const UClass* GetObjectiveClass() const;
	void OnObjectiveClassChanged(const UClass* NewClass);

	// Cached reference
	UQuestlineNode_Step* StepNode = nullptr;

	// Actors in all levels whose QuestTargetComponent watches this step's tag.
	// Populated at the start of UpdateGraphNode() and consumed by summary + expanded content.
	TArray<FString> WatchingTargetNames;

	// Actors in all levels whose QuestGiverComponent watches this step's tag.
	// Populated at the start of UpdateGraphNode() and consumed by summary + expanded content.
	TArray<FString> WatchingGiverNames;

};
