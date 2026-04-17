// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Nodes/Slate/SGraphNode_GroupNode.h"
#include "Nodes/Groups/QuestlineNode_GroupSetterBase.h"
#include "Nodes/Groups/QuestlineNode_GroupGetterBase.h"
#include "SGraphPin.h"
#include "SGameplayTagCombo.h"
#include "ScopedTransaction.h"
#include "GraphEditorSettings.h"
#include "IDocumentation.h"
#include "SCommentBubble.h"
#include "TutorialMetaData.h"
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
	SetterNode = Cast<UQuestlineNode_GroupSetterBase>(InNode);
	GetterNode = Cast<UQuestlineNode_GroupGetterBase>(InNode);
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

	this->ContentScale.Bind(this, &SGraphNode::GetContentScale);

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

	// Tag picker — separate row for setters only; getters embed it inline with output pin
	if (bIsSetter)
	{
		InnerVerticalBox->AddSlot()
			.AutoHeight()
			.Padding(FMargin(10.f, 4.f, 10.f, 1.f))
			[
				CreateTagPickerWidget()
			];
	}

	// Pin content area — 2px bottom padding for input column breathing room
	InnerVerticalBox->AddSlot()
		.AutoHeight()
		.HAlign(HAlign_Fill)
		.Padding(0.f, 0.f, 0.f, 4.f)
		[
			CreatePinContentArea()
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

	// Left (input) pin column — centered vertically
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
	else if (bIsSetter)
	{
		// Single-input setter (activation setter): output pin only, centered.
		PinOverlay->AddSlot()
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
			[
				SAssignNew(RightNodeBox, SVerticalBox)
			];
	}
	else
	{
		// Getter: tag picker inline with output pin on same row for flat layout.
		PinOverlay->AddSlot()
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(14.f, 0.f, 4.f, 0.f)
				[
					CreateTagPickerWidget()
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SAssignNew(RightNodeBox, SVerticalBox)
				]
			];
	}

	// Minimum height so straddling works even with a single input pin
	return SNew(SBox)
		.MinDesiredHeight(48.f)
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
	if (SetterNode) FilterString = SetterNode->GetGroupFilterString();
	else if (GetterNode) FilterString = GetterNode->GetGroupFilterString();

	return SNew(SGameplayTagCombo)
		.Filter(*FilterString)
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
	if (!GraphNode) return;

	const FScopedTransaction Transaction(LOCTEXT("ChangeGroupTag", "Change Group Tag"));
	GraphNode->Modify();

	if (SetterNode) SetterNode->SetGroupTag(NewTag);
	else if (GetterNode) GetterNode->SetGroupTag(NewTag);

	UpdateGraphNode();

	if (UEdGraph* Graph = GraphNode->GetGraph())
	{
		Graph->NotifyGraphChanged();
	}
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