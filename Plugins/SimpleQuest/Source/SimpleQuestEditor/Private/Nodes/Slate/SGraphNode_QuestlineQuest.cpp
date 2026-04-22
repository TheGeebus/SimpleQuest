// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Nodes/Slate/SGraphNode_QuestlineQuest.h"

#include "Nodes/QuestlineNode_Quest.h"
#include "Nodes/Slate/SGraphNode_QuestContentHelpers.h"
#include "Utilities/SimpleQuestEditorUtils.h"
#include "GameplayTagContainer.h"
#include "GraphEditorSettings.h"
#include "IDocumentation.h"
#include "SCommentBubble.h"
#include "SimpleQuestLog.h"
#include "TutorialMetaData.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Text/STextBlock.h"

#define QUEST_GIVER_COLOR FLinearColor(0.75f, 0.4f, 1.f)   // matches Step's STEP_GIVER_COLOR so givers read consistently across widgets

#define LOCTEXT_NAMESPACE "SGraphNode_QuestlineQuest"

void SGraphNode_QuestlineQuest::Construct(const FArguments& InArgs, UQuestlineNode_Quest* InNode)
{
	QuestNode = InNode;
	GraphNode = InNode;
	SetCursor(EMouseCursor::CardinalCross);
	UpdateGraphNode();
}

void SGraphNode_QuestlineQuest::UpdateGraphNode()
{
	// Refresh watching-givers cache + stale-tag flag. Empty giver list collapses the display via BuildLabeledExpandableList.
	// Contextual givers (attached via OUTER-asset inlinings) append with "(via OuterAssetName)" annotation.
	WatchingGiverNames.Reset();
	if (QuestNode)
	{
		QuestNode->bTagStale = !FSimpleQuestEditorUtilities::IsContentNodeTagCurrent(QuestNode);

		const FGameplayTag CompiledTag = FSimpleQuestEditorUtilities::FindCompiledTagForNode(QuestNode);
		if (CompiledTag.IsValid())
		{
			WatchingGiverNames = FSimpleQuestEditorUtilities::FindActorNamesGivingTag(CompiledTag);
		}
		for (const FSimpleQuestEditorUtilities::FQuestContextualGiver& Entry
			: FSimpleQuestEditorUtilities::FindContextualGiversForNode(QuestNode))
		{
			WatchingGiverNames.Add(FString::Printf(TEXT("%s (via %s)"),
				*Entry.ActorName, *Entry.OuterAssetDisplayName.ToString()));
		}
	}

	InputPins.Empty();
	OutputPins.Empty();
	RightNodeBox.Reset();
	LeftNodeBox.Reset();

	SetupErrorReporting();

	const FSlateBrush* NodeBodyBrush = GetNodeBodyBrush();

	// ── Title area ─────────────────────────────────────────────
	// Same boilerplate as Step / LinkedQuestline. Factoring into a shared helper makes sense once there's a fourth
	// consumer; premature with three.
	TSharedPtr<SNodeTitle> NodeTitle = SNew(SNodeTitle, GraphNode);

	IconColor = FLinearColor::White;
	const FSlateBrush* IconBrush = nullptr;
	if (GraphNode && GraphNode->ShowPaletteIconOnNode())
	{
		IconBrush = GraphNode->GetIconAndTint(IconColor).GetOptionalIcon();
	}

	TSharedRef<SOverlay> DefaultTitleAreaWidget =
		SNew(SOverlay)
		+ SOverlay::Slot().HAlign(HAlign_Fill).VAlign(VAlign_Fill)
		[
			SNew(SImage).Image(FAppStyle::GetBrush("Graph.Node.TitleGloss"))
			.ColorAndOpacity(this, &SGraphNode::GetNodeTitleIconColor)
		]
		+ SOverlay::Slot().HAlign(HAlign_Fill).VAlign(VAlign_Fill)
		[
			SNew(SBorder).BorderImage(FAppStyle::GetBrush("Graph.Node.ColorSpill")).Padding(TitleBorderMargin)
			.BorderBackgroundColor(this, &SGraphNode::GetNodeTitleColor)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().VAlign(VAlign_Top).Padding(FMargin(0.f, 0.f, 4.f, 0.f)).AutoWidth()
				[
					SNew(SImage).Image(IconBrush).ColorAndOpacity(this, &SGraphNode::GetNodeTitleIconColor)
				]
				+ SHorizontalBox::Slot()
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight() [ CreateTitleWidget(NodeTitle) ]
					+ SVerticalBox::Slot().AutoHeight() [ NodeTitle.ToSharedRef() ]
				]
			]
		]
		+ SOverlay::Slot().VAlign(VAlign_Top)
		[
			SNew(SBorder).Visibility(EVisibility::HitTestInvisible)
			.BorderImage(FAppStyle::GetBrush("Graph.Node.TitleHighlight"))
			.BorderBackgroundColor(this, &SGraphNode::GetNodeTitleIconColor)
			[ SNew(SSpacer).Size(FVector2D(20, 20)) ]
		];

	SetDefaultTitleAreaWidget(DefaultTitleAreaWidget);

	// ── Tooltip ────────────────────────────────────────────────
	if (!SWidget::GetToolTip().IsValid())
	{
		TSharedRef<SToolTip> DefaultToolTip = IDocumentation::Get()->CreateToolTip(
			TAttribute<FText>(this, &SGraphNode::GetNodeTooltip), nullptr,
			GraphNode->GetDocumentationLink(), GraphNode->GetDocumentationExcerptName());
		SetToolTip(DefaultToolTip);
	}

	// ── Inner content ──────────────────────────────────────────
	FGraphNodeMetaData TagMeta(TEXT("Graphnode"));
	PopulateMetaTag(&TagMeta);

	this->ContentScale.Bind(this, &SGraphNode::GetContentScale);

	TSharedPtr<SVerticalBox> InnerVerticalBox = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Fill).VAlign(VAlign_Top)
		.Padding(Settings->GetNonPinNodeBodyPadding())
		[
			DefaultTitleAreaWidget
		]

		// Pin content area — Quest pins (Activate input, outcome outputs, Deactivate if enabled).
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Fill).VAlign(VAlign_Top)
		[
			CreateNodeContentArea()
		]

		// Stale tag warning bar (visible after rename, before recompile).
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(4.f, 2.f, 4.f, 0.f))
		[
			FQuestNodeSlateHelpers::BuildStaleTagWarningBar(
				TAttribute<bool>::CreateLambda([this]() { return QuestNode && QuestNode->bTagStale; }))
		]

		// Givers section — empty-list collapses to SNullWidget automatically via the helper.
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(14.f, 2.f, 10.f, 8.f))
		[
			FQuestNodeSlateHelpers::BuildLabeledExpandableList(
				LOCTEXT("GiversLabel", "Givers"),
				WatchingGiverNames,
				QUEST_GIVER_COLOR,
				[this]() { return QuestNode && QuestNode->bGiversExpanded; },
				[this]() { if (QuestNode) QuestNode->bGiversExpanded = !QuestNode->bGiversExpanded; })
		];

	TSharedPtr<SWidget> EnabledStateWidget = GetEnabledStateWidget();
	if (EnabledStateWidget.IsValid())
	{
		InnerVerticalBox->AddSlot().AutoHeight().HAlign(HAlign_Fill).VAlign(VAlign_Top).Padding(FMargin(2, 0))
			[ EnabledStateWidget.ToSharedRef() ];
	}

	InnerVerticalBox->AddSlot().AutoHeight().Padding(Settings->GetNonPinNodeBodyPadding())
		[ ErrorReporting->AsWidget() ];
	InnerVerticalBox->AddSlot().AutoHeight().Padding(Settings->GetNonPinNodeBodyPadding())
		[ VisualWarningReporting->AsWidget() ];

	// ── Assemble outer structure ───────────────────────────────
	TSharedPtr<SVerticalBox> MainVerticalBox;
	this->GetOrAddSlot(ENodeZone::Center).HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SAssignNew(MainVerticalBox, SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SOverlay).AddMetaData<FGraphNodeMetaData>(TagMeta)
				+ SOverlay::Slot().Padding(Settings->GetNonPinNodeBodyPadding())
				[
					SNew(SImage).Image(NodeBodyBrush).ColorAndOpacity(this, &SGraphNode::GetNodeBodyColor)
				]
				+ SOverlay::Slot()
				[
					InnerVerticalBox.ToSharedRef()
				]
			]
		];

	// ── Comment bubble ─────────────────────────────────────────
	if (GraphNode == nullptr || GraphNode->SupportsCommentBubble())
	{
		TSharedPtr<SCommentBubble> CommentBubble;
		const FSlateColor CommentColor = GetDefault<UGraphEditorSettings>()->DefaultCommentNodeTitleColor;

		SAssignNew(CommentBubble, SCommentBubble)
			.GraphNode(GraphNode)
			.Text(this, &SGraphNode::GetNodeComment)
			.OnTextCommitted(this, &SGraphNode::OnCommentTextCommitted)
			.OnToggled(this, &SGraphNode::OnCommentBubbleToggled)
			.ColorAndOpacity(CommentColor)
			.AllowPinning(true).EnableTitleBarBubble(true).EnableBubbleCtrls(true)
			.GraphLOD(this, &SGraphNode::GetCurrentLOD)
			.IsGraphNodeHovered(this, &SGraphNode::IsHovered);

		GetOrAddSlot(ENodeZone::TopCenter)
			.SlotOffset2f(TAttribute<FVector2f>(CommentBubble.Get(), &SCommentBubble::GetOffset2f))
			.SlotSize2f(TAttribute<FVector2f>(CommentBubble.Get(), &SCommentBubble::GetSize2f))
			.AllowScaling(TAttribute<bool>(CommentBubble.Get(), &SCommentBubble::IsScalingAllowed))
			.VAlign(VAlign_Top)
			[ CommentBubble.ToSharedRef() ];
	}

	// ── Finalize ───────────────────────────────────────────────
	CreateBelowWidgetControls(MainVerticalBox);
	CreatePinWidgets();
	CreateBelowPinControls(InnerVerticalBox);
}

#undef LOCTEXT_NAMESPACE
#undef QUEST_GIVER_COLOR