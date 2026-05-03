// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Nodes/Slate/SGraphNode_UtilityNode.h"
#include "Nodes/Utility/QuestlineNode_UtilityBase.h"
#include "Nodes/Utility/QuestlineNode_SetBlocked.h"
#include "SGraphPin.h"
#include "SGameplayTagContainerCombo.h"
#include "ScopedTransaction.h"
#include "GraphEditorSettings.h"
#include "IDocumentation.h"
#include "SCommentBubble.h"
#include "TutorialMetaData.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SGraphNode_UtilityNode"

void SGraphNode_UtilityNode::Construct(const FArguments& InArgs, UEdGraphNode* InNode)
{
	GraphNode = InNode;
	UtilityNode = Cast<UQuestlineNode_UtilityBase>(InNode);
	SetCursor(EMouseCursor::CardinalCross);
	UpdateGraphNode();
}

void SGraphNode_UtilityNode::UpdateGraphNode()
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
		]



		// Pin content area — 2px bottom padding for input column breathing room
		+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Fill)
		[
			CreatePinContentArea()
		]

		// Tag picker
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FMargin(10.f, 4.f, 10.f, 4.f))
		[
			CreateTagPickerWidget()
		]

		// Optional toggles row — currently only SetBlocked surfaces a toggle here ("Also Deactivate Targets").
		// CreateAlsoDeactivateToggleWidget returns SNullWidget for any other utility node type, so the slot
		// collapses to zero size in those cases (padding lives inside the wrapper widget).
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.f, 0.f, 0.f, 4.f)
		[
			CreateAlsoDeactivateToggleWidget()
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
}

TSharedRef<SWidget> SGraphNode_UtilityNode::CreatePinContentArea()
{
	TSharedRef<SHorizontalBox> PinBox = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Center)
		.FillWidth(1.0f)
		[
			SAssignNew(LeftNodeBox, SVerticalBox)
		]
		+ SHorizontalBox::Slot()
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Center)
		.AutoWidth()
		.Padding(20.f, 0.f, 0.f, 0.f)
		[
			SAssignNew(RightNodeBox, SVerticalBox)
		];

	return PinBox;
}

void SGraphNode_UtilityNode::CreatePinWidgets()
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

void SGraphNode_UtilityNode::AddPin(const TSharedRef<SGraphPin>& PinToAdd)
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

TSharedRef<SWidget> SGraphNode_UtilityNode::CreateTagPickerWidget()
{
	FString FilterString = UtilityNode ? UtilityNode->GetTargetQuestTagsFilterString() : FString(TEXT("Quest"));

	return SNew(SWrapBox).PreferredSize(200.f).Orientation(Orient_Horizontal)
		+ SWrapBox::Slot().Padding(8.f, 0.f, 2.f, 0.f).VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("TagsToBlockLabel", "Tags to Block:"))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
		]
		+ SWrapBox::Slot().Padding(4.f, 2.f, 0.f, 2.f)
		[
			SNew(SGameplayTagContainerCombo)
			.Filter(*FilterString)
			.TagContainer_Lambda([this]()
			{
				if (UtilityNode) return UtilityNode->GetTargetQuestTags();
				static const FGameplayTagContainer Empty;
				return Empty;
			})
			.OnTagContainerChanged_Lambda([this](const FGameplayTagContainer& NewTags)
			{
				OnTargetTagsChanged(NewTags);
			})
		];
	/*
	return SNew(SGameplayTagContainerCombo)
		.Filter(*FilterString)
		.TagContainer_Lambda([this]()
		{
			if (UtilityNode) return UtilityNode->GetTargetQuestTags();
			static const FGameplayTagContainer Empty;
			return Empty;
		})
		.OnTagContainerChanged_Lambda([this](const FGameplayTagContainer& NewTags)
		{
			OnTargetTagsChanged(NewTags);
		});
		*/
}

void SGraphNode_UtilityNode::OnTargetTagsChanged(const FGameplayTagContainer& NewTags)
{
	if (!GraphNode || !UtilityNode) return;

	const FScopedTransaction Transaction(LOCTEXT("ChangeUtilityTags", "Change Utility Target Tags"));
	GraphNode->Modify();

	UtilityNode->SetTargetQuestTags(NewTags);

	if (UEdGraph* Graph = GraphNode->GetGraph())
	{
		Graph->NotifyGraphChanged();
	}
}

TSharedRef<SWidget> SGraphNode_UtilityNode::CreateAlsoDeactivateToggleWidget()
{
	UQuestlineNode_SetBlocked* SetBlockedNode = Cast<UQuestlineNode_SetBlocked>(UtilityNode);
	if (!SetBlockedNode) return SNullWidget::NullWidget;

	return SNew(SBox)
	.Padding(FMargin(18.f, 4.f, 10.f, 8.f))
	[
		SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([SetBlockedNode]()
				{
					return SetBlockedNode->bAlsoDeactivateTargets ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged(this, &SGraphNode_UtilityNode::OnAlsoDeactivateChanged)
				.ToolTipText(LOCTEXT("AlsoDeactivateTooltip",
					"When checked, also issues a deactivation request for each target quest in addition to setting the "
					"Blocked re-entry gate. By default, blocking only sets the gate — any in-flight lifecycle on the targets "
					"continues to its current resolution."))
			]
			+ SHorizontalBox::Slot().Padding(4.f, 0.f, 0.f, 0.f).FillWidth(1.f).HAlign(HAlign_Left).VAlign(VAlign_Center)
			[			
				SNew(STextBlock)
				.Text(LOCTEXT("AlsoDeactivateLabel", "Also Deactivate"))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
			]
	];
}

void SGraphNode_UtilityNode::OnAlsoDeactivateChanged(ECheckBoxState NewState)
{
	UQuestlineNode_SetBlocked* SetBlockedNode = Cast<UQuestlineNode_SetBlocked>(UtilityNode);
	if (!SetBlockedNode || !GraphNode) return;

	const FScopedTransaction Transaction(LOCTEXT("ChangeAlsoDeactivate", "Toggle Also Deactivate Targets"));
	GraphNode->Modify();

	SetBlockedNode->bAlsoDeactivateTargets = (NewState == ECheckBoxState::Checked);

	if (UEdGraph* Graph = GraphNode->GetGraph())
	{
		Graph->NotifyGraphChanged();
	}
}

#undef LOCTEXT_NAMESPACE