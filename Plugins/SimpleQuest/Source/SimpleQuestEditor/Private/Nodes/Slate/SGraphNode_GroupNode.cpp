// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Nodes/Slate/SGraphNode_GroupNode.h"
#include "Nodes/Groups/QuestlineNode_PortalEntryBase.h"
#include "Nodes/Groups/QuestlineNode_PortalExitBase.h"
#include "SGraphPin.h"
#include "ScopedTransaction.h"
#include "GraphEditorSettings.h"
#include "IDocumentation.h"
#include "SCommentBubble.h"
#include "SGameplayTagCombo.h"
#include "TutorialMetaData.h"
#include "Nodes/Slate/SGraphNode_QuestContentHelpers.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SGraphNode_GroupNode"

void SGraphNode_GroupNode::Construct(const FArguments& InArgs, UEdGraphNode* InNode)
{
	GraphNode = InNode;
	SetterNode = Cast<UQuestlineNode_PortalEntryBase>(InNode);
	GetterNode = Cast<UQuestlineNode_PortalExitBase>(InNode);
	bIsSetter = (SetterNode != nullptr);
	SetCursor(EMouseCursor::CardinalCross);
	UpdateGraphNode();
}

void SGraphNode_GroupNode::UpdateGraphNode()
{
	InputPins.Empty();
	OutputPins.Empty();
	RightNodeBox.Reset();
	LeftNodeBox.Reset();
	RightColumn.Reset();

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
		+ SOverlay::Slot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		[
			SNew(SImage)
			.Image(FAppStyle::GetBrush("Graph.Node.TitleGloss"))
			.ColorAndOpacity(this, &SGraphNode::GetNodeTitleIconColor)
		]
		+ SOverlay::Slot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("Graph.Node.ColorSpill"))
			.Padding(TitleBorderMargin)
			.BorderBackgroundColor(this, &SGraphNode::GetNodeTitleColor)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Top)
				.Padding(FMargin(0.f, 0.f, 4.f, 0.f))
				.AutoWidth()
				[
					SNew(SImage)
					.Image(IconBrush)
					.ColorAndOpacity(this, &SGraphNode::GetNodeTitleIconColor)
				]
				+ SHorizontalBox::Slot()
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						CreateTitleWidget(NodeTitle)
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						NodeTitle.ToSharedRef()
					]
				]
			]
		]
		+ SOverlay::Slot()
		.VAlign(VAlign_Top)
		[
			SNew(SBorder)
			.Visibility(EVisibility::HitTestInvisible)
			.BorderImage(FAppStyle::GetBrush("Graph.Node.TitleHighlight"))
			.BorderBackgroundColor(this, &SGraphNode::GetNodeTitleIconColor)
			[
				SNew(SSpacer)
				.Size(FVector2D(20, 20))
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

	TSharedPtr<SVerticalBox> InnerVerticalBox = SNew(SVerticalBox)

		// Title
		+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Top)
		.Padding(EditorSettings->GetNonPinNodeBodyPadding())
		[
			DefaultTitleAreaWidget
		];

	// Pin content area, directly under the title so this node's heading lines up with every other non-content
	// node's when they are wired together - a wire pins two pin rows to the same height, so anything above a pin
	// row pushes that node's heading out of line. No padding of its own; the picker below supplies the gap.
	InnerVerticalBox->AddSlot()
		.AutoHeight()
		.HAlign(HAlign_Fill)
		[
			CreatePinContentArea()
		];

	// Tag picker - its own row on both halves of the pair. A getter has no input pin to sit beneath, but the rule
	// is about where the picker goes, not about how many pins a node happens to have: an Entry and an Exit are two
	// halves of one mechanism and should not read as two kinds of node.
	InnerVerticalBox->AddSlot()
		.AutoHeight()
		.HAlign(HAlign_Left)
		.Padding(FMargin(10.f, 4.f, 10.f, 8.f))
		[
			CreateTagPickerWidget()
		];

	// Enabled state widget
	TSharedPtr<SWidget> EnabledStateWidget = GetEnabledStateWidget();
	if (EnabledStateWidget.IsValid())
	{
		InnerVerticalBox->AddSlot()
			.AutoHeight()
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Top)
			[
				EnabledStateWidget.ToSharedRef()
			];
	}

	InnerVerticalBox->AddSlot()
		.AutoHeight()
		.Padding(EditorSettings->GetNonPinNodeBodyPadding())
		[
			ErrorReporting->AsWidget()
		];

	this->GetOrAddSlot(ENodeZone::Center)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SOverlay)
				.AddMetaData<FGraphNodeMetaData>(TagMeta)
				+ SOverlay::Slot()
				.Padding(EditorSettings->GetNonPinNodeBodyPadding())
				[
					SNew(SImage)
					.Image(GetNodeBodyBrush())
					.ColorAndOpacity(this, &SGraphNode::GetNodeBodyColor)
				]
				+ SOverlay::Slot()
				[
					InnerVerticalBox.ToSharedRef()
				]
			]
		];

	// Comment bubble
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
		[
			CommentBubble.ToSharedRef()
		];

	CreatePinWidgets();

	// Deferred: add pin button into RightColumn AFTER CreatePinWidgets so it sits below the output pin.
	if (bIsSetter && SetterNode && SetterNode->CanAddInputPin() && RightColumn.IsValid())
	{
		FMargin AddPinPadding = EditorSettings->GetOutputPinPadding();
		AddPinPadding.Top += 6.0f;

		RightColumn->AddSlot()
			.FillHeight(1.0f)
			.VAlign(VAlign_Top)
			.HAlign(HAlign_Right)
			.Padding(AddPinPadding.Left, AddPinPadding.Top + 2.f, AddPinPadding.Right, AddPinPadding.Bottom)
			[
				CreateAddPinButton()
			];
	}
}

TSharedRef<SWidget> SGraphNode_GroupNode::CreatePinContentArea()
{
	TSharedRef<SOverlay> PinOverlay = SNew(SOverlay);

	// Left (input) pin column - centered vertically
	PinOverlay->AddSlot()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Center)
		[
			SAssignNew(LeftNodeBox, SVerticalBox)
		];

	if (SetterNode && SetterNode->CanAddInputPin())
	{
		// Right side: two-cell layout straddling the centerline.
		// Output pin bottom-aligned in top half; add-pin button added later in bottom half.
		PinOverlay->AddSlot()
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Fill)
			.Padding(20.f, 0.f, 0.f, 0.f)
			[
				SAssignNew(RightColumn, SVerticalBox)
				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				.VAlign(VAlign_Bottom)
				[
					SAssignNew(RightNodeBox, SVerticalBox)
				]
			];
	}
	else
	{
		// Single-input setter and getter alike: output pin only, right-aligned. The getter used to embed its tag
		// picker here on the pin row, which is what made it render narrower than its own Entry - the picker was
		// competing with the pin for one row's width instead of getting a row of its own.
		PinOverlay->AddSlot()
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
			[
				SAssignNew(RightNodeBox, SVerticalBox)
			];
	}

	// Minimum height ONLY for the two-cell straddle, which needs room to split an output pin into the top half
	// and an add-pin button into the bottom. A plain pin row sizes to its pins, and forcing a floor on it leaves
	// a void under the title now that the picker sits below the pins rather than above them.
	const bool bNeedsStraddleHeight = SetterNode && SetterNode->CanAddInputPin();
	return SNew(SBox)
		.MinDesiredHeight(bNeedsStraddleHeight ? 48.f : 0.f)
		[
			PinOverlay
		];
}

void SGraphNode_GroupNode::CreatePinWidgets()
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

void SGraphNode_GroupNode::AddPin(const TSharedRef<SGraphPin>& PinToAdd)
{
	PinToAdd->SetOwner(SharedThis(this));
	PinToAdd->SetShowLabel(false);

	if (PinToAdd->GetDirection() == EEdGraphPinDirection::EGPD_Input)
	{
		FMargin PinPadding = Settings->GetInputPinPadding();
		PinPadding.Top += 1.f;
		PinPadding.Bottom += 1.f;

		LeftNodeBox->AddSlot()
			.AutoHeight()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.Padding(PinPadding)
			[
				PinToAdd
			];
		InputPins.Add(PinToAdd);
	}
	else
	{
		FMargin PinPadding = Settings->GetOutputPinPadding();
		PinPadding.Top += 1.f;
		PinPadding.Bottom += 1.f;

		RightNodeBox->AddSlot()
			.AutoHeight()
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
			.Padding(PinPadding)
			[
				PinToAdd
			];
		OutputPins.Add(PinToAdd);
	}
}

TSharedRef<SWidget> SGraphNode_GroupNode::CreateTagPickerWidget()
{
	FString FilterString;
	if (SetterNode) FilterString = SetterNode->GetTagFilterString();
	else if (GetterNode) FilterString = GetterNode->GetTagFilterString();

	return SNew(SGameplayTagCombo)
		.Filter(FilterString)
		.Tag_Lambda([this]()
		{
			if (SetterNode) return SetterNode->GetGroupTag();
			if (GetterNode) return GetterNode->GetGroupTag();
			return FGameplayTag();
		})
		.OnTagChanged_Lambda([this](const FGameplayTag NewTag)
		{
			OnGroupTagChanged(NewTag);
		});
}

TSharedRef<SWidget> SGraphNode_GroupNode::CreateAddPinButton()
{
	TSharedRef<SButton> Button = SNew(SButton)
		.ContentPadding(0.0f)
		.ButtonStyle(FAppStyle::Get(), "NoBorder")
		.OnClicked(this, &SGraphNode_GroupNode::OnAddPinClicked)
		.IsEnabled(this, &SGraphNode::IsNodeEditable)
		.ToolTipText(LOCTEXT("AddPinTooltip", "Add another input pin"))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.HAlign(HAlign_Left)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("AddPin", "Add pin"))
				.ColorAndOpacity(FLinearColor::White)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(7, 0, 0, 0)
			[
				SNew(SImage)
				.Image(FAppStyle::GetBrush(TEXT("Icons.PlusCircle")))
			]
		];

	Button->SetCursor(EMouseCursor::Hand);
	return Button;
}

void SGraphNode_GroupNode::OnGroupTagChanged(const FGameplayTag NewTag)
{
	TWeakObjectPtr<UQuestlineNode_PortalEntryBase> WeakSetter = SetterNode;
	TWeakObjectPtr<UQuestlineNode_PortalExitBase>  WeakGetter = GetterNode;

	FQuestNodeSlateHelpers::CommitNodeEditDeferred(GraphNode,
		LOCTEXT("ChangeGroupTag", "Change Group Tag"),
		[WeakSetter, WeakGetter, NewTag]()
		{
			if (UQuestlineNode_PortalEntryBase* Setter = WeakSetter.Get())     Setter->SetGroupTag(NewTag);
			else if (UQuestlineNode_PortalExitBase* Getter = WeakGetter.Get()) Getter->SetGroupTag(NewTag);
		});
}

FReply SGraphNode_GroupNode::OnAddPinClicked()
{
	if (SetterNode)
	{
		const FScopedTransaction Transaction(LOCTEXT("AddGroupInputPin", "Add Group Input Pin"));
		SetterNode->AddInputPin();
		UpdateGraphNode();
	}
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE