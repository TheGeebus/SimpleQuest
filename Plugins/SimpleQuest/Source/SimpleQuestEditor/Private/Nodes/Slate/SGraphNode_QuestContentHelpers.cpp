// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Nodes/Slate/SGraphNode_QuestContentHelpers.h"

#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SGraphNode_QuestContentHelpers"

namespace FQuestNodeSlateHelpers
{
	TSharedRef<SWidget> BuildLabeledExpandableList(
		const FText& Label,
		const TArray<FString>& Items,
		const FLinearColor& Color,
		TFunction<bool()> IsExpanded,
		TFunction<void()> ToggleExpanded)
	{
		if (Items.Num() == 0)
		{
			return SNullWidget::NullWidget;
		}

		// Items column: first item always visible, rest expand below it.
		TSharedRef<SVerticalBox> ItemsColumn = SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
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
				AdditionalItems->AddSlot().AutoHeight()
				[
					SNew(STextBlock)
					.Text(FText::FromString(Items[i]))
					.ColorAndOpacity(FSlateColor(Color))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				];
			}

			ItemsColumn->AddSlot().AutoHeight() [ AdditionalItems ];
		}

		return SNew(SHorizontalBox)

			// Expand/collapse chevron.
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(0.f, 0.f, 4.f, 0.f)
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

			// Label.
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top)
			[
				SNew(STextBlock)
				.Text(FText::Format(LOCTEXT("LabelFmt", "{0}: "), Label))
				.ColorAndOpacity(FSlateColor(Color))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
			]

			// Items column.
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Top)
			[
				ItemsColumn
			];
	}
}

#undef LOCTEXT_NAMESPACE