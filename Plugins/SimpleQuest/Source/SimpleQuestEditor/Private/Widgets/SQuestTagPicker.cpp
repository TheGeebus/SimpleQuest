// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Widgets/SQuestTagPicker.h"

#include "Utilities/QuestTagComposer.h"
#include "SGameplayTagChip.h"
#include "SGameplayTagPicker.h"
#include "Widgets/Input/SMenuAnchor.h"

#define LOCTEXT_NAMESPACE "SQuestTagPicker"

void SQuestTagPicker::Construct(const FArguments& InArgs)
{
	TagAttribute = InArgs._Tag;
	OnTagChangedDelegate = InArgs._OnTagChanged;
	Filter = InArgs._Filter;
	bReadOnly = InArgs._ReadOnly;

	// SMenuAnchor wrapping the chip directly — no SComboButton frame in between. The chip's own SButton
	// handles the click via OnEditPressed → toggles the menu anchor open. Net layout footprint is exactly
	// the chip's DesiredSize; no surrounding padding gutters.
	ChildSlot
	[
		SAssignNew(MenuAnchor, SMenuAnchor)
		.Placement(MenuPlacement_BelowAnchor)
		.OnGetMenuContent(this, &SQuestTagPicker::OnGetMenuContent)
		[
			SNew(SGameplayTagChip)
			.ReadOnly(bReadOnly)
			.ShowClearButton(true)
			.Text(this, &SQuestTagPicker::GetDisplayText)
			.ToolTipText(this, &SQuestTagPicker::GetTooltipText)
			.OnEditPressed(this, &SQuestTagPicker::OnChipPressed)
			.OnClearPressed(this, &SQuestTagPicker::OnClearPressed)
		]
	];
}

FText SQuestTagPicker::GetDisplayText() const
{
	const FGameplayTag Current = TagAttribute.Get();
	return Current.IsValid()
		? FQuestTagComposer::FormatTagForDisplay(Current.GetTagName())
		: LOCTEXT("EmptyTag", "(none)");
}

FText SQuestTagPicker::GetTooltipText() const
{
	// Surface the full tag in the tooltip — designers hover to disambiguate when the short form is ambiguous,
	// and copy-paste workflows expect to see the canonical identifier.
	const FGameplayTag Current = TagAttribute.Get();
	return Current.IsValid()
		? FText::FromString(Current.ToString())
		: LOCTEXT("EmptyTagTooltip", "No tag set — click to pick one.");
}

FReply SQuestTagPicker::OnChipPressed()
{
	// Toggle the dropdown — clicking the chip again with the dropdown open closes it (matches typical
	// combo-box click semantics).
	if (!bReadOnly && MenuAnchor.IsValid())
	{
		MenuAnchor->SetIsOpen(!MenuAnchor->IsOpen());
	}
	return FReply::Handled();
}

FReply SQuestTagPicker::OnClearPressed()
{
	if (!bReadOnly)
	{
		OnTagChangedDelegate.ExecuteIfBound(FGameplayTag());
	}
	return FReply::Handled();
}

TSharedRef<SWidget> SQuestTagPicker::OnGetMenuContent()
{
	TArray<FGameplayTagContainer> InitialContainers;
	FGameplayTagContainer InitialContainer;
	const FGameplayTag Current = TagAttribute.Get();
	if (Current.IsValid()) InitialContainer.AddTag(Current);
	InitialContainers.Add(InitialContainer);

	return SNew(SGameplayTagPicker)
		.Filter(Filter)
		.ReadOnly(bReadOnly)
		.MultiSelect(false)
		.TagContainers(InitialContainers)
		.OnTagChanged(this, &SQuestTagPicker::OnPickerTagSelected);
}

void SQuestTagPicker::OnPickerTagSelected(const TArray<FGameplayTagContainer>& TagContainers)
{
	FGameplayTag NewTag;
	if (TagContainers.Num() > 0 && TagContainers[0].Num() > 0)
	{
		NewTag = TagContainers[0].First();
	}
	OnTagChangedDelegate.ExecuteIfBound(NewTag);

	if (MenuAnchor.IsValid())
	{
		MenuAnchor->SetIsOpen(false);
	}
}

#undef LOCTEXT_NAMESPACE