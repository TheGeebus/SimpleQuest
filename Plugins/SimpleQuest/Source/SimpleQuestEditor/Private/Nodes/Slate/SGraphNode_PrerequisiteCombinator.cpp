// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Nodes/Slate/SGraphNode_PrerequisiteCombinator.h"

#include "GraphEditorSettings.h"
#include "IDocumentation.h"
#include "SCommentBubble.h"
#include "SGraphPin.h"
#include "SNodePanel.h"
#include "TutorialMetaData.h"
#include "Nodes/Prerequisites/QuestlineNode_PrerequisiteBase.h"
#include "Nodes/Prerequisites/QuestlineNode_PrerequisiteAnd.h"
#include "Nodes/Prerequisites/QuestlineNode_PrerequisiteOr.h"
#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SGraphNode_PrerequisiteCombinator"

void SGraphNode_PrerequisiteCombinator::Construct(const FArguments& InArgs, UQuestlineNode_PrerequisiteBase* InNode)
{
    CombinatorNode = InNode;
    GraphNode = InNode;
    bCanAddPin = InNode->IsA<UQuestlineNode_PrerequisiteAnd>()
              || InNode->IsA<UQuestlineNode_PrerequisiteOr>();
    SetCursor(EMouseCursor::CardinalCross);
    UpdateGraphNode();
}

void SGraphNode_PrerequisiteCombinator::UpdateGraphNode()
{
    InputPins.Empty();
    OutputPins.Empty();

    SetupErrorReporting();

    RightNodeBox.Reset();
    LeftNodeBox.Reset();

    TSharedPtr<SToolTip> NodeToolTip = SNew(SToolTip);
    if (!GraphNode->GetTooltipText().IsEmpty())
    {
        NodeToolTip = IDocumentation::Get()->CreateToolTip(
            TAttribute<FText>(this, &SGraphNode::GetNodeTooltip),
            nullptr, GraphNode->GetDocumentationLink(),
            GraphNode->GetDocumentationExcerptName());
    }

    FGraphNodeMetaData TagMeta(TEXT("Graphnode"));
    PopulateMetaTag(&TagMeta);

    TSharedPtr<SNodeTitle> NodeTitle = SNew(SNodeTitle, GraphNode);

    TSharedRef<SOverlay> NodeOverlay = SNew(SOverlay);

    // Center glyph
    NodeOverlay->AddSlot()
        .HAlign(HAlign_Center)
        .VAlign(VAlign_Center)
        .Padding(45.f, 0.f, 45.f, 0.f)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot()
            .HAlign(HAlign_Center)
            .AutoHeight()
            [
                SNew(STextBlock)
                .TextStyle(FAppStyle::Get(), "Graph.CompactNode.Title")
                .Text(NodeTitle.Get(), &SNodeTitle::GetHeadTitle)
                .WrapTextAt(128.0f)
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                NodeTitle.ToSharedRef()
            ]
        ];

    static const float MinNodePadding = 55.f;

    // Left (input) pin column
    NodeOverlay->AddSlot()
        .HAlign(HAlign_Left)
        .VAlign(VAlign_Center)
        .Padding(0.f, 0.f, MinNodePadding, 0.f)        [
            SAssignNew(LeftNodeBox, SVerticalBox)
        ];

    // Right (output) side
    TSharedPtr<SVerticalBox> RightColumn;
    if (bCanAddPin)
    {
        // Two-cell layout: output pin and add button each fill half the height,
        // aligned toward each other so they straddle the vertical centerline.
        NodeOverlay->AddSlot()
            .HAlign(HAlign_Right)
            .VAlign(VAlign_Fill)
            .Padding(MinNodePadding + 20, 14.f, 0.f, 0.f)
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
        // NOT node: single output, centered
        NodeOverlay->AddSlot()
            .HAlign(HAlign_Right)
            .VAlign(VAlign_Center)
            .Padding(MinNodePadding, 0.f, 0.f, 0.f)
            [
                SAssignNew(RightNodeBox, SVerticalBox)
            ];
    }

    this->ContentScale.Bind(this, &SGraphNode::GetContentScale);

    TSharedRef<SVerticalBox> InnerVerticalBox =
        SNew(SVerticalBox)
        + SVerticalBox::Slot()
        [
            SNew(SOverlay)
            + SOverlay::Slot()
            [
                SNew(SImage)
                .Image(FAppStyle::GetBrush("Graph.VarNode.Body"))
            ]
            + SOverlay::Slot()
            [
                SNew(SImage)
                .Image(FAppStyle::GetBrush("Graph.VarNode.Gloss"))
            ]
            + SOverlay::Slot()
            .Padding(FMargin(0, 3))
            [
                NodeOverlay
            ]
        ];

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

    InnerVerticalBox->AddSlot()
        .AutoHeight()
        .Padding(FMargin(5.0f, 1.0f))
        [
            ErrorReporting->AsWidget()
        ];

    this->GetOrAddSlot(ENodeZone::Center)
        .HAlign(HAlign_Center)
        .VAlign(VAlign_Center)
        [
            InnerVerticalBox
        ];

    CreatePinWidgets();
    
    // Add pin button — right side, below output pin
    if (bCanAddPin && RightColumn.IsValid())
    {
        FMargin AddPinPadding = Settings->GetOutputPinPadding();
        AddPinPadding.Top += 6.0f;

        TSharedRef<SButton> AddPinButton = SNew(SButton)
            .ContentPadding(0.0f)
            .ButtonStyle(FAppStyle::Get(), "NoBorder")
            .OnClicked(this, &SGraphNode_PrerequisiteCombinator::OnAddPin)
            .IsEnabled(this, &SGraphNode::IsNodeEditable)
            .ToolTipText(LOCTEXT("AddPinTooltip", "Add another condition input"))
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

        AddPinButton->SetCursor(EMouseCursor::Hand);

        RightColumn->AddSlot()
            .FillHeight(1.0f)
            .VAlign(VAlign_Top)
            .HAlign(HAlign_Right)
            .Padding(AddPinPadding.Left, AddPinPadding.Top + 2.f, AddPinPadding.Right, AddPinPadding.Bottom)
            [
                AddPinButton
            ];
    }

    // Comment bubble
    TSharedPtr<SCommentBubble> CommentBubble;
    const FSlateColor CommentColor = GetDefault<UGraphEditorSettings>()->DefaultCommentNodeTitleColor;

    SAssignNew(CommentBubble, SCommentBubble)
        .GraphNode(GraphNode)
        .Text(this, &SGraphNode::GetNodeComment)
        .OnTextCommitted(this, &SGraphNode::OnCommentTextCommitted)
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

FReply SGraphNode_PrerequisiteCombinator::OnAddPin()
{
    if (auto* AndNode = Cast<UQuestlineNode_PrerequisiteAnd>(GraphNode))
    {
        AndNode->AddConditionPin();
    }
    else if (auto* OrNode = Cast<UQuestlineNode_PrerequisiteOr>(GraphNode))
    {
        OrNode->AddConditionPin();
    }

    UpdateGraphNode();

    return FReply::Handled();
}

void SGraphNode_PrerequisiteCombinator::AddPin(const TSharedRef<SGraphPin>& PinToAdd)
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

#undef LOCTEXT_NAMESPACE
