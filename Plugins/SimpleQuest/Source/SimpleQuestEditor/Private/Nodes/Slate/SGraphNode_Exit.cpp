// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Nodes/Slate/SGraphNode_Exit.h"

#include "Nodes/QuestlineNode_Exit.h"
#include "SGraphPin.h"
#include "Widgets/SQuestTagPicker.h"
#include "ScopedTransaction.h"
#include "GraphEditorSettings.h"
#include "IDocumentation.h"
#include "SCommentBubble.h"
#include "TutorialMetaData.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SGraphNode_Exit"

void SGraphNode_Exit::Construct(const FArguments& InArgs, UQuestlineNode_Exit* InNode)
{
	GraphNode = InNode;
	ExitNode = InNode;
	SetCursor(EMouseCursor::CardinalCross);
	UpdateGraphNode();
}

void SGraphNode_Exit::UpdateGraphNode()
{
	InputPins.Empty();
	OutputPins.Empty();
	RightNodeBox.Reset();
	LeftNodeBox.Reset();

	SetupErrorReporting();

	const UGraphEditorSettings* EditorSettings = GetDefault<UGraphEditorSettings>();

	// ── Title area ─────────────────────────────────────────────
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
			SNew(SBorder).BorderImage(FAppStyle::GetBrush("Graph.Node.ColorSpill"))
				.Padding(TitleBorderMargin)
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
			[
				SNew(SSpacer).Size(FVector2D(20, 20))
			]
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
		// Title
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Fill).VAlign(VAlign_Top)
		.Padding(EditorSettings->GetNonPinNodeBodyPadding())
		[ DefaultTitleAreaWidget ]

		// Outcome tag picker: filtered to SimpleQuest.QuestOutcome namespace. HAlign_Left so the slot
		// shrinks to the picker's content width instead of stretching to fill the node body horizontally.
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(FMargin(10.f, 4.f, 10.f, 4.f))
		[ CreateTagPickerWidget() ]

		// Pin content area (single Outcome input pin)
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Fill).Padding(0.f, 0.f, 0.f, 2.f)
		[ CreatePinContentArea() ];

	InnerVerticalBox->AddSlot().AutoHeight().Padding(EditorSettings->GetNonPinNodeBodyPadding())
		[ ErrorReporting->AsWidget() ];

	this->GetOrAddSlot(ENodeZone::Center).HAlign(HAlign_Center).VAlign(VAlign_Center)
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SOverlay).AddMetaData<FGraphNodeMetaData>(TagMeta)
			+ SOverlay::Slot().Padding(EditorSettings->GetNonPinNodeBodyPadding())
			[
				SNew(SImage).Image(GetNodeBodyBrush()).ColorAndOpacity(this, &SGraphNode::GetNodeBodyColor)
			]
			+ SOverlay::Slot() [ InnerVerticalBox.ToSharedRef() ]
		]
	];

	// ── Comment bubble ─────────────────────────────────────────
	TSharedPtr<SCommentBubble> CommentBubble;
	const FSlateColor CommentColor = GetDefault<UGraphEditorSettings>()->DefaultCommentNodeTitleColor;

	SAssignNew(CommentBubble, SCommentBubble)
		.GraphNode(GraphNode)
		.Text(this, &SGraphNode::GetNodeComment)
		.OnTextCommitted(this, &SGraphNode::OnCommentTextCommitted)
		.OnToggled(this, &SGraphNode::OnCommentBubbleToggled)
		.ColorAndOpacity(CommentColor)
		.AllowPinning(true)
		.EnableTitleBarBubble(true)
		.EnableBubbleCtrls(true)
		.GraphLOD(this, &SGraphNode::GetCurrentLOD)
		.IsGraphNodeHovered(this, &SGraphNode::IsHovered);

	GetOrAddSlot(ENodeZone::TopCenter)
		.SlotOffset2f(TAttribute<FVector2f>(CommentBubble.Get(), &SCommentBubble::GetOffset2f))
		.SlotSize2f(TAttribute<FVector2f>(CommentBubble.Get(), &SCommentBubble::GetSize2f))
		.AllowScaling(TAttribute<bool>(CommentBubble.Get(), &SCommentBubble::IsScalingAllowed))
		[ CommentBubble.ToSharedRef() ];

	CreatePinWidgets();
}

TSharedRef<SWidget> SGraphNode_Exit::CreatePinContentArea()
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().HAlign(HAlign_Left).VAlign(VAlign_Center).FillWidth(1.0f)
		[ SAssignNew(LeftNodeBox, SVerticalBox) ]
		+ SHorizontalBox::Slot().HAlign(HAlign_Right).VAlign(VAlign_Center).AutoWidth().Padding(20.f, 0.f, 0.f, 0.f)
		[ SAssignNew(RightNodeBox, SVerticalBox) ];
}

void SGraphNode_Exit::CreatePinWidgets()
{
	for (UEdGraphPin* CurPin : GraphNode->Pins)
	{
		if (!CurPin->bHidden)
		{
			TSharedPtr<SGraphPin> NewPin = CreatePinWidget(CurPin);
			check(NewPin.IsValid());
			AddPin(NewPin.ToSharedRef());
		}
	}
}

void SGraphNode_Exit::AddPin(const TSharedRef<SGraphPin>& PinToAdd)
{
	PinToAdd->SetOwner(SharedThis(this));
	// Hide the "Outcome" pin label — the title + picker already communicate what the pin is for.
	PinToAdd->SetShowLabel(false);

	if (PinToAdd->GetDirection() == EEdGraphPinDirection::EGPD_Input)
	{
		FMargin PinPadding = Settings->GetInputPinPadding();
		PinPadding.Top += 1.f;
		PinPadding.Bottom += 1.f;
		LeftNodeBox->AddSlot().AutoHeight().HAlign(HAlign_Left).VAlign(VAlign_Center).Padding(PinPadding)
			[ PinToAdd ];
		InputPins.Add(PinToAdd);
	}
	else
	{
		FMargin PinPadding = Settings->GetOutputPinPadding();
		PinPadding.Top += 1.f;
		PinPadding.Bottom += 1.f;
		RightNodeBox->AddSlot().AutoHeight().HAlign(HAlign_Right).VAlign(VAlign_Center).Padding(PinPadding)
			[ PinToAdd ];
		OutputPins.Add(PinToAdd);
	}
}

TSharedRef<SWidget> SGraphNode_Exit::CreateTagPickerWidget()
{
	// Filter matches the UPROPERTY meta = (Categories = "SimpleQuest.QuestOutcome") on UQuestlineNode_Exit::OutcomeTag —
	// picker surfaces only tags under that root, same as the Details-panel picker.
	return SNew(SQuestTagPicker)
		.Filter(TEXT("SimpleQuest.QuestOutcome"))
		.Tag_Lambda([this]()
		{
			return ExitNode ? ExitNode->OutcomeTag : FGameplayTag();
		})
		.OnTagChanged_Lambda([this](const FGameplayTag NewTag)
		{
			OnOutcomeTagChanged(NewTag);
		});
}

void SGraphNode_Exit::OnOutcomeTagChanged(const FGameplayTag NewTag)
{
	if (!GraphNode || !ExitNode) return;

	const FScopedTransaction Transaction(LOCTEXT("ChangeOutcomeTag", "Change Outcome Tag"));
	GraphNode->Modify();

	ExitNode->OutcomeTag = NewTag;

	// NotifyGraphChanged triggers SNodeTitle to re-query GetNodeTitle so the "Outcome - <leaf>" title updates
	// immediately. Matches the existing PostEditChangeProperty path used when the Details panel picker drives
	// the change.
	if (UEdGraph* Graph = GraphNode->GetGraph())
	{
		Graph->NotifyGraphChanged();
	}
}

#undef LOCTEXT_NAMESPACE