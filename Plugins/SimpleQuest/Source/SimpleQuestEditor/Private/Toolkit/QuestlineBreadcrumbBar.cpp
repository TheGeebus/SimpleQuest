// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Toolkit/QuestlineBreadcrumbBar.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

void SQuestlineBreadcrumbBar::Construct(const FArguments& InArgs)
{
    OnCrumbClicked = InArgs._OnCrumbClicked;
    OnDelimiterClicked = InArgs._OnDelimiterClicked;

    ChildSlot[ SAssignNew(Box, SHorizontalBox) ];
}

void SQuestlineBreadcrumbBar::SetCrumbs(const TArray<FQuestlineBreadcrumb>& InCrumbs)
{
    Crumbs = InCrumbs;
    Rebuild();
}

void SQuestlineBreadcrumbBar::Rebuild()
{
    Box->ClearChildren();

    for (int32 i = 0; i < Crumbs.Num(); ++i)
    {
        UEdGraph* Graph      = Crumbs[i].Graph;
        FText     Label      = Crumbs[i].DisplayName;

        Box->AddSlot()
        .AutoWidth()
        .VAlign(VAlign_Center)
        [
            SNew(SButton)
            .ButtonStyle(FAppStyle::Get(), "GraphBreadcrumbButton")
            .OnClicked_Lambda([this, Graph]()
            {
                OnCrumbClicked.ExecuteIfBound(Graph);
                return FReply::Handled();
            })
            [
                SNew(STextBlock)
                .TextStyle(FAppStyle::Get(), "GraphBreadcrumbButtonText")
                .Text(Label)
            ]
        ];

        if (i < Crumbs.Num() - 1)
        {
            UEdGraph*     ParentGraph = Crumbs[i].Graph;
            UEdGraphNode* EntryNode   = Crumbs[i + 1].EntryNode;

            Box->AddSlot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            [
                SNew(SButton)
                .ButtonStyle(FAppStyle::Get(), "GraphBreadcrumbButton")
                .OnClicked_Lambda([this, ParentGraph, EntryNode]()
                {
                    OnDelimiterClicked.ExecuteIfBound(ParentGraph, EntryNode);
                    return FReply::Handled();
                })
                [
                    SNew(SImage)
                    .Image(FAppStyle::GetBrush("BreadcrumbTrail.Delimiter"))
                ]
            ];
        }
    }
}
