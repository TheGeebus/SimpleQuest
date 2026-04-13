// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Nodes/Slate/SGraphNode_QuestlineStep.h"

#include "Nodes/QuestlineNode_Step.h"
#include "Objectives/QuestObjective.h"
#include "Quests/Types/QuestStepEnums.h"
#include "GraphEditorSettings.h"
#include "IDocumentation.h"
#include "SCommentBubble.h"
#include "TutorialMetaData.h"
#include "Rewards/QuestReward.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Text/STextBlock.h"
#include "PropertyCustomizationHelpers.h"
#include "Utilities/SimpleQuestEditorUtils.h"
#include "Widgets/Layout/SSeparator.h"


#define STEP_INFO_TEXT_COLOR	FLinearColor(0.9f, 0.9f, 0.9f)
#define STEP_ACTOR_COLOR		FLinearColor(0.8f, 0.5f, 0.17f)
#define STEP_CLASS_COLOR		FLinearColor(0.2f, 0.325f, 0.85f)
#define STEP_ELEMENT_COLOR		FLinearColor(0.25f, 0.8f, 0.20f)
#define STEP_GIVER_COLOR		FLinearColor(0.75f, 0.4f, 1.f)

#define LOCTEXT_NAMESPACE "SGraphNode_QuestlineStep"

void SGraphNode_QuestlineStep::Construct(const FArguments& InArgs, UQuestlineNode_Step* InNode)
{
	StepNode = InNode;
	GraphNode = InNode;
	SetCursor(EMouseCursor::CardinalCross);
	UpdateGraphNode();
}

void SGraphNode_QuestlineStep::UpdateGraphNode()
{
	// Query givers and targets watching this step's tag — consumed by summary and expanded content
	WatchingGiverNames.Reset();
	WatchingTargetNames.Reset();
	if (StepNode)
	{
		const FGameplayTag StepTag = USimpleQuestEditorUtilities::ReconstructStepTag(StepNode);
		if (StepTag.IsValid())
		{
			WatchingGiverNames = USimpleQuestEditorUtilities::FindActorNamesGivingTag(StepTag);
			WatchingTargetNames = USimpleQuestEditorUtilities::FindActorNamesWatchingTag(StepTag);
		}
	}

	InputPins.Empty();
	OutputPins.Empty();
	RightNodeBox.Reset();
	LeftNodeBox.Reset();

	SetupErrorReporting();

	const FSlateBrush* NodeBodyBrush = GetNodeBodyBrush();

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

	TSharedPtr<SVerticalBox> InnerVerticalBox;
	InnerVerticalBox = SNew(SVerticalBox)

		// Title
		+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Top)
		.Padding(Settings->GetNonPinNodeBodyPadding())
		[
			DefaultTitleAreaWidget
		]

		// Objective info (always visible)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FMargin(10.f, 4.f, 10.f, 0.f))
		[
			CreateObjectiveInfoWidget()
		]

		// Target summary (always visible when populated)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FMargin(24.f, 3.f, 10.f, 1.f))
		[
			CreateTargetSummaryWidget()
		]

		// Pin content area
		+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Top)
		[
			CreateNodeContentArea()
		]
		
		// Separator between pins and expanded info
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FMargin(12.f, 2.f, 10.f, 4.f))
		[
			SNew(SSeparator)
			.Thickness(2.f)
			.ColorAndOpacity(FLinearColor::White)
			.Visibility(this, &SGraphNode_QuestlineStep::GetExpandedContentVisibility)
		]

		// Expanded content (visibility toggled)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FMargin(12.f, 2.f, 10.f, 0.f))
		[
			CreateExpandedContentWidget()
		];

	// Enabled state widget (disabled / development-only banner)
	TSharedPtr<SWidget> EnabledStateWidget = GetEnabledStateWidget();
	if (EnabledStateWidget.IsValid())
	{
		InnerVerticalBox->AddSlot()
			.AutoHeight()
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Top)
			.Padding(FMargin(2, 0))
			[
				EnabledStateWidget.ToSharedRef()
			];
	}

	// Error / warning reporting
	InnerVerticalBox->AddSlot()
		.AutoHeight()
		.Padding(Settings->GetNonPinNodeBodyPadding())
		[
			ErrorReporting->AsWidget()
		];

	InnerVerticalBox->AddSlot()
		.AutoHeight()
		.Padding(Settings->GetNonPinNodeBodyPadding())
		[
			VisualWarningReporting->AsWidget()
		];

	// ── Assemble outer structure ───────────────────────────────
	TSharedPtr<SVerticalBox> MainVerticalBox;
	this->GetOrAddSlot(ENodeZone::Center)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SAssignNew(MainVerticalBox, SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				// Red border wrapper — draws a tinted body brush behind the node. When ObjectiveClass is null, the outer
				// brush is bright red, creating a visible outline through the 2px padding gap. When set, the outer brush matches
				// the body color and the border is invisible.
				SNew(SOverlay)
				.AddMetaData<FGraphNodeMetaData>(TagMeta)
				+ SOverlay::Slot()
				.Padding(Settings->GetNonPinNodeBodyPadding())
				[
					SNew(SImage)
					.Image(NodeBodyBrush)
					.ColorAndOpacity(this, &SGraphNode::GetNodeBodyColor)
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
			.AllowPinning(true)
			.EnableTitleBarBubble(true)
			.EnableBubbleCtrls(true)
			.GraphLOD(this, &SGraphNode::GetCurrentLOD)
			.IsGraphNodeHovered(this, &SGraphNode::IsHovered);

		GetOrAddSlot(ENodeZone::TopCenter)
			.SlotOffset2f(TAttribute<FVector2f>(CommentBubble.Get(), &SCommentBubble::GetOffset2f))
			.SlotSize2f(TAttribute<FVector2f>(CommentBubble.Get(), &SCommentBubble::GetSize2f))
			.AllowScaling(TAttribute<bool>(CommentBubble.Get(), &SCommentBubble::IsScalingAllowed))
			.VAlign(VAlign_Top)
			[
				CommentBubble.ToSharedRef()
			];
	}

	// ── Finalize ───────────────────────────────────────────────
	CreateBelowWidgetControls(MainVerticalBox);
	CreatePinWidgets();
	CreateBelowPinControls(InnerVerticalBox);
	CreateExpandCollapseArrow(InnerVerticalBox);
}


// ── Body Content Builders ──────────────────────────────────────

TSharedRef<SWidget> SGraphNode_QuestlineStep::CreateObjectiveInfoWidget()
{
	return SNew(SClassPropertyEntryBox)
		.MetaClass(UQuestObjective::StaticClass())
		.AllowNone(true)
		.AllowAbstract(false)
		.IsBlueprintBaseOnly(false)
		.SelectedClass(this, &SGraphNode_QuestlineStep::GetObjectiveClass)
		.OnSetClass(this, &SGraphNode_QuestlineStep::OnObjectiveClassChanged);
}

TSharedRef<SWidget> SGraphNode_QuestlineStep::CreateTargetSummaryWidget()
{
	if (!StepNode) return SNullWidget::NullWidget;

	const int32 ActorCount = WatchingTargetNames.Num();
	const int32 GiverCount = WatchingGiverNames.Num();

	int32 ClassCount = 0;
	for (const TSubclassOf<AActor>& Class : StepNode->TargetClasses)
	{
		if (Class) ClassCount++;
	}

	const int32 ElementCount = StepNode->NumberOfElements;

	if (ActorCount == 0 && GiverCount == 0 && ClassCount == 0 && ElementCount <= 0)
	{
		return SNullWidget::NullWidget;
	}

	TSharedRef<SHorizontalBox> SummaryBox = SNew(SHorizontalBox);
	bool bNeedsSeparator = false;

	auto AddSeparator = [&]()
	{
		if (bNeedsSeparator)
		{
			SummaryBox->AddSlot()
				.AutoWidth()
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT(" \u00B7 ")))
					.ColorAndOpacity(FSlateColor(STEP_INFO_TEXT_COLOR))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				];
		}
	};

	if (GiverCount > 0)
	{
		SummaryBox->AddSlot()
			.AutoWidth()
			[
				SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(TEXT("Givers: %d"), GiverCount)))
				.ColorAndOpacity(FSlateColor(STEP_GIVER_COLOR))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
			];
		bNeedsSeparator = true;
	}

	if (ActorCount > 0)
	{
		AddSeparator();
		SummaryBox->AddSlot()
			.AutoWidth()
			[
				SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(TEXT("Actors: %d"), ActorCount)))
				.ColorAndOpacity(FSlateColor(STEP_ACTOR_COLOR))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
			];
		bNeedsSeparator = true;
	}
	
	if (ClassCount > 0)
	{
		AddSeparator();
		SummaryBox->AddSlot()
			.AutoWidth()
			[
				SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(TEXT("Classes: %d"), ClassCount)))
				.ColorAndOpacity(FSlateColor(STEP_CLASS_COLOR))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
			];
		bNeedsSeparator = true;
	}

	if (ElementCount > 0)
	{
		AddSeparator();
		SummaryBox->AddSlot()
			.AutoWidth()
			[
				SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(TEXT("\u00D7%d"), ElementCount)))
				.ColorAndOpacity(FSlateColor(STEP_ELEMENT_COLOR))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
			];
	}

	return SummaryBox;
}

TSharedRef<SWidget> SGraphNode_QuestlineStep::CreateExpandedContentWidget()
{
	// Actor names come from the live query computed in UpdateGraphNode()
	const TArray<FString>& ActorNames = WatchingTargetNames;

	// ── Compute class names at construction time ──────────────
	TArray<FString> ClassNames;
	if (StepNode)
	{
		for (const TSubclassOf<AActor>& Class : StepNode->TargetClasses)
		{
			if (Class)
			{
				FString Name = Class->GetName();
				Name.RemoveFromEnd(TEXT("_C"));
				ClassNames.Add(Name);
			}
		}
	}

	// ── Build the expanded view ────────────────────────────────
	return SNew(SVerticalBox)
		.Visibility(this, &SGraphNode_QuestlineStep::GetExpandedContentVisibility)

		// Quest givers (expandable list)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.f, 1.f)
		[
			BuildTargetList(
				LOCTEXT("GiversLabel", "Givers"),
				WatchingGiverNames,
				STEP_GIVER_COLOR,
				[this]() { return StepNode && StepNode->bGiversExpanded; },
				[this]() { if (StepNode) StepNode->bGiversExpanded = !StepNode->bGiversExpanded; })
		]

		// Target actors (expandable list)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.f, 1.f)
		[
			BuildTargetList(
				LOCTEXT("TargetActorsLabel", "Targets"),
				ActorNames,
				STEP_ACTOR_COLOR,
				[this]() { return StepNode && StepNode->bTargetActorsExpanded; },
				[this]() { if (StepNode) StepNode->bTargetActorsExpanded = !StepNode->bTargetActorsExpanded; })
		]

		// Target classes (expandable list)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.f, 1.f)
		[
			BuildTargetList(
				LOCTEXT("TargetClassesLabel", "Classes"),
				ClassNames,
				STEP_CLASS_COLOR,
				[this]() { return StepNode && StepNode->bTargetClassesExpanded; },
				[this]() { if (StepNode) StepNode->bTargetClassesExpanded = !StepNode->bTargetClassesExpanded; })
		]

		// Reward class
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FMargin(14.f, 1.f, 0.f, 1.f))
		[
			SNew(STextBlock)
			.Text_Lambda([this]()
			{
				if (!StepNode || !StepNode->RewardClass) return FText::GetEmpty();
				FString Name = StepNode->RewardClass->GetName();
				Name.RemoveFromEnd(TEXT("_C"));
				return FText::Format(LOCTEXT("RewardFmt", "Reward: {0}"),
					FText::FromString(Name));
			})
			.Visibility_Lambda([this]()
			{
				return (StepNode && StepNode->RewardClass)
					? EVisibility::Visible : EVisibility::Collapsed;
			})
			.ColorAndOpacity(FSlateColor(STEP_INFO_TEXT_COLOR))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
		]

		// Prerequisite gate mode
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FMargin(14.f, 1.f, 0.f, 1.f))
		[
			SNew(STextBlock)
			.Text_Lambda([this]()
			{
				if (!StepNode) return FText::GetEmpty();
				switch (StepNode->PrerequisiteGateMode)
				{
				case EPrerequisiteGateMode::GatesProgression:
					return LOCTEXT("GateProgression", "Gate: Progression");
				case EPrerequisiteGateMode::GatesCompletion:
					return LOCTEXT("GateCompletion", "Gate: Completion");
				default:
					return FText::GetEmpty();
				}
			})
			.ColorAndOpacity(FSlateColor(STEP_INFO_TEXT_COLOR))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
		]

		// Target vector (only when non-zero)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FMargin(14.f, 1.f, 0.f, 1.f))
		[
			SNew(STextBlock)
			.Text_Lambda([this]()
			{
				if (!StepNode) return FText::GetEmpty();
				const FVector& V = StepNode->TargetVector;
				return FText::FromString(FString::Printf(TEXT("Vector: (%g, %g, %g)"), V.X, V.Y, V.Z));
			})
			.Visibility_Lambda([this]()
			{
				return (StepNode && !StepNode->TargetVector.IsZero())
					? EVisibility::Visible : EVisibility::Collapsed;
			})
			.ColorAndOpacity(FSlateColor(STEP_INFO_TEXT_COLOR))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
		];
}


// ── Expand / Collapse ──────────────────────────────────────────

void SGraphNode_QuestlineStep::CreateExpandCollapseArrow(TSharedPtr<SVerticalBox> MainBox)
{
	if (!MainBox.IsValid()) return;

	MainBox->AddSlot()
		.AutoHeight()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Top)
		.Padding(3, 0, 3, 3)
		[
			SNew(SCheckBox)
			.OnCheckStateChanged(this, &SGraphNode_QuestlineStep::OnExpandCollapseChanged)
			.IsChecked(this, &SGraphNode_QuestlineStep::GetExpandCollapseState)
			.Cursor(EMouseCursor::Default)
			.Style(FAppStyle::Get(), "Graph.Node.AdvancedView")
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Center)
				[
					SNew(SImage)
					.Image(this, &SGraphNode_QuestlineStep::GetExpandCollapseArrow)
				]
			]
		];
}

void SGraphNode_QuestlineStep::OnExpandCollapseChanged(const ECheckBoxState NewState)
{
	const bool bExpanded = (NewState == ECheckBoxState::Checked);

	// Persist on the node so the state survives widget rebuilds
	if (StepNode)
	{
		StepNode->bStepDetailExpanded = bExpanded;
	}
}

ECheckBoxState SGraphNode_QuestlineStep::GetExpandCollapseState() const
{
	return (StepNode && StepNode->bStepDetailExpanded)
		? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

const FSlateBrush* SGraphNode_QuestlineStep::GetExpandCollapseArrow() const
{
	const bool bExpanded = StepNode && StepNode->bStepDetailExpanded;
	return FAppStyle::GetBrush(bExpanded ? TEXT("Icons.ChevronUp") : TEXT("Icons.ChevronDown"));
}

EVisibility SGraphNode_QuestlineStep::GetExpandedContentVisibility() const
{
	return (StepNode && StepNode->bStepDetailExpanded)
		? EVisibility::Visible : EVisibility::Collapsed;
}


// ── Red Border ─────────────────────────────────────────────────

int32 SGraphNode_QuestlineStep::OnPaint(
	const FPaintArgs& Args,
	const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements,
	int32 LayerId,
	const FWidgetStyle& InWidgetStyle,
	bool bParentEnabled) const
{
	if (StepNode && !StepNode->ObjectiveClass)
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

	return SGraphNode::OnPaint(Args, AllottedGeometry, MyCullingRect,
		OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
}

// ── Objective Picker ───────────────────────────────────────────

const UClass* SGraphNode_QuestlineStep::GetObjectiveClass() const
{
	return StepNode ? StepNode->ObjectiveClass.Get() : nullptr;
}

void SGraphNode_QuestlineStep::OnObjectiveClassChanged(const UClass* NewClass)
{
	if (!StepNode) return;

	const FScopedTransaction Transaction(LOCTEXT("ChangeObjective", "Change Objective Class"));
	StepNode->Modify();
	StepNode->ObjectiveClass = const_cast<UClass*>(NewClass);

	// Outcome pins may change — refresh them
	StepNode->RefreshOutcomePins();

	// Always notify even if pins didn't change — our visual state did
	// (red border, target summary, details panel sync)
	if (UEdGraph* Graph = StepNode->GetGraph())
	{
		Graph->NotifyGraphChanged();
	}
}

TSharedRef<SWidget> SGraphNode_QuestlineStep::BuildTargetList(const FText& Label, const TArray<FString>& Items, const FLinearColor& Color,
	TFunction<bool()> IsExpanded, TFunction<void()> ToggleExpanded)
{
	if (Items.Num() == 0)
	{
		return SNullWidget::NullWidget;
	}

	// ── Items column: first item always visible, rest expand below it ──
	TSharedRef<SVerticalBox> ItemsColumn = SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(STextBlock)
			.Text(FText::FromString(Items[0]))
			.ColorAndOpacity(FSlateColor(Color))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
		];

	if (Items.Num() > 1)
	{
		TSharedRef<SVerticalBox> AdditionalItems = SNew(SVerticalBox)
			.Visibility_Lambda([IsExpanded]()
			{
				return IsExpanded() ? EVisibility::Visible : EVisibility::Collapsed;
			});

		for (int32 i = 1; i < Items.Num(); ++i)
		{
			AdditionalItems->AddSlot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.Text(FText::FromString(Items[i]))
					.ColorAndOpacity(FSlateColor(Color))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				];
		}

		ItemsColumn->AddSlot()
			.AutoHeight()
			[
				AdditionalItems
			];
	}

	// ── Full row: Arrow | Label | Items ──
	return SNew(SHorizontalBox)

		// Expand/collapse arrow
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Top)
		.Padding(0.f, 0.f, 4.f, 0.f)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "NoBorder")
			.ContentPadding(0)
			.Cursor(EMouseCursor::Default)
			.Visibility(Items.Num() > 1 ? EVisibility::Visible : EVisibility::Hidden)
			.OnClicked_Lambda([ToggleExpanded]()
			{
				ToggleExpanded();
				return FReply::Handled();
			})
			[
				SNew(SImage)
				.Image_Lambda([IsExpanded]() -> const FSlateBrush*
				{
					return FAppStyle::GetBrush(
						IsExpanded() ? TEXT("TreeArrow_Expanded") : TEXT("TreeArrow_Collapsed"));
				})
				.ColorAndOpacity(FSlateColor(Color))
				.DesiredSizeOverride(FVector2D(10.0, 10.0))
			]
		]

		// Label — takes its natural text width
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Top)
		[
			SNew(STextBlock)
			.Text(FText::Format(LOCTEXT("TargetLabelFmt", "{0}: "), Label))
			.ColorAndOpacity(FSlateColor(Color))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
		]

		// Items — stacked vertically, all aligned to the same left edge
		+ SHorizontalBox::Slot()
		.FillWidth(1.f)
		.VAlign(VAlign_Top)
		[
			ItemsColumn
		];
}

#undef LOCTEXT_NAMESPACE
