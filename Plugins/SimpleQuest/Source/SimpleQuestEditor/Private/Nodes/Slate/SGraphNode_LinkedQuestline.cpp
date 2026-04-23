// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Nodes/Slate/SGraphNode_LinkedQuestline.h"
#include "Nodes/Slate/SGraphNode_QuestContentHelpers.h"
#include "GameplayTagContainer.h"
#include "Nodes/QuestlineNode_LinkedQuestline.h"
#include "Quests/QuestlineGraph.h"
#include "GraphEditorSettings.h"
#include "IDocumentation.h"
#include "SCommentBubble.h"
#include "ScopedTransaction.h"
#include "TutorialMetaData.h"
#include "PropertyCustomizationHelpers.h"
#include "SimpleQuestLog.h"
#include "Styling/AppStyle.h"
#include "Utilities/SimpleQuestEditorUtils.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SGraphNode_LinkedQuestline"

#define LINKED_GIVER_COLOR FLinearColor(0.75f, 0.4f, 1.f)

void SGraphNode_LinkedQuestline::Construct(const FArguments& InArgs, UQuestlineNode_LinkedQuestline* InNode)
{
	LinkedNode = InNode;
	GraphNode = InNode;
	SetCursor(EMouseCursor::CardinalCross);
	UpdateGraphNode();
}

void SGraphNode_LinkedQuestline::UpdateGraphNode()
{
	// Standard content-node giver path + stale-tag flag. Contextual givers append with "(via OuterAssetName)" annotation.
	WatchingGiverNames.Reset();
	if (LinkedNode)
	{
		LinkedNode->bTagStale = !FSimpleQuestEditorUtilities::IsContentNodeTagCurrent(LinkedNode);

		const FGameplayTag CompiledTag = FSimpleQuestEditorUtilities::FindCompiledTagForNode(LinkedNode);
		if (CompiledTag.IsValid())
		{
			WatchingGiverNames = FSimpleQuestEditorUtilities::FindActorNamesGivingTag(CompiledTag);
		}
		for (const FSimpleQuestEditorUtilities::FQuestContextualActor& Entry
			: FSimpleQuestEditorUtilities::FindContextualGiversForNode(LinkedNode))
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
	// Mirrors SGraphNode_QuestlineStep's title boilerplate so custom content nodes share a consistent look.
	// Factoring this into a shared helper becomes worthwhile once a third consumer lands — premature now.
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

		// Asset picker — the primary body of this node.
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(10.f, 4.f, 10.f, 4.f))
		[
			CreateAssetPickerWidget()
		]

		// Pin content area
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Fill).VAlign(VAlign_Top)
		[
			CreateNodeContentArea()
		]

		// Stale tag warning bar (visible after rename, before recompile).
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(4.f, 2.f, 4.f, 0.f))
		[
			FQuestNodeSlateHelpers::BuildStaleTagWarningBar(
				TAttribute<bool>::CreateLambda([this]() { return LinkedNode && LinkedNode->bTagStale; }))
		]

		// Givers section — empty-list collapses to SNullWidget automatically via the helper.
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(14.f, 2.f, 10.f, 10.f))
		[
			FQuestNodeSlateHelpers::BuildLabeledExpandableList(
				LOCTEXT("GiversLabel", "Givers"),
				WatchingGiverNames,
				LINKED_GIVER_COLOR,
				[this]() { return LinkedNode && LinkedNode->bGiversExpanded; },
				[this]() { if (LinkedNode) LinkedNode->bGiversExpanded = !LinkedNode->bGiversExpanded; })
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

TSharedRef<SWidget> SGraphNode_LinkedQuestline::CreateAssetPickerWidget()
{
	// SObjectPropertyEntryBox filtered to UQuestlineGraph. Displays the current asset's short name + a browse-to-asset
	// button + a "use current content browser selection" pull-in. Self-reference filtered at the picker level; deeper
	// cycle detection is a compile-time concern, already handled by the compiler.
	return SNew(SObjectPropertyEntryBox)
		.AllowedClass(UQuestlineGraph::StaticClass())
		.ObjectPath(this, &SGraphNode_LinkedQuestline::GetAssetPath)
		.OnObjectChanged(this, &SGraphNode_LinkedQuestline::OnAssetChanged)
		.OnShouldFilterAsset(this, &SGraphNode_LinkedQuestline::OnShouldFilterAsset)
		.AllowClear(true)
		.DisplayThumbnail(false)
		.DisplayBrowse(true)
		.DisplayUseSelected(true);
}

FString SGraphNode_LinkedQuestline::GetAssetPath() const
{
	if (LinkedNode && !LinkedNode->LinkedGraph.IsNull())
	{
		return LinkedNode->LinkedGraph.ToSoftObjectPath().ToString();
	}
	return FString();
}

void SGraphNode_LinkedQuestline::OnAssetChanged(const FAssetData& NewAsset)
{
	if (!LinkedNode) return;

	const FScopedTransaction Transaction(LOCTEXT("ChangeLinkedQuestline", "Change Linked Questline"));
	LinkedNode->Modify();

	// NewAsset.GetAsset() sync-loads on first access — expected when the designer is actively selecting.
	UQuestlineGraph* NewGraph = Cast<UQuestlineGraph>(NewAsset.GetAsset());
	LinkedNode->LinkedGraph = NewGraph;
	LinkedNode->RebuildOutcomePinsFromLinkedGraph();

	// NotifyGraphChanged drives SNodeTitle to re-query GetNodeTitle, so "Linked Questline - <name>" reflects the
	// new asset's FriendlyName immediately.
	if (UEdGraph* Graph = LinkedNode->GetGraph())
	{
		Graph->NotifyGraphChanged();
	}
}

bool SGraphNode_LinkedQuestline::OnShouldFilterAsset(const FAssetData& AssetData) const
{
	// Reject self-reference (this node's owning questline asset picking itself). Deeper cycle detection — chains
	// that route back to the same asset through one or more intermediate LinkedQuestlines — is a compile-time concern.
	if (!LinkedNode) return false;
	UEdGraph* MyGraph = LinkedNode->GetGraph();
	if (!MyGraph) return false;
	UObject* Outer = MyGraph->GetOuter();
	while (Outer && !Outer->IsA<UQuestlineGraph>())
	{
		Outer = Outer->GetOuter();
	}
	if (!Outer) return false;
	return AssetData.GetSoftObjectPath() == FSoftObjectPath(Outer);
}

int32 SGraphNode_LinkedQuestline::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	// Red shadow outline when no asset is assigned — matches Step's ObjectiveClass-null affordance so incomplete
	// configuration reads as a visible error at a glance.
	if (LinkedNode && LinkedNode->LinkedGraph.IsNull())
	{
		const FVector2f ShadowInflate = UE::Slate::CastToVector2f(
			GetDefault<UGraphEditorSettings>()->GetShadowDeltaSize());

		FSlateDrawElement::MakeBox(
			OutDrawElements,
			LayerId,
			AllottedGeometry.ToInflatedPaintGeometry(ShadowInflate),
			FAppStyle::GetBrush(TEXT("Graph.Node.ShadowSelected")),
			ESlateDrawEffect::None,
			FLinearColor(1.0f, 0.0f, 0.0f, 0.75f)
		);
	}

	return SGraphNode::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
}

#undef LOCTEXT_NAMESPACE