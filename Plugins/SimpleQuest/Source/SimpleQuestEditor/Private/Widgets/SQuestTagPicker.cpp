// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Widgets/SQuestTagPicker.h"

#include "Framework/Application/SlateApplication.h"
#include "Utilities/QuestTagComposer.h"
#include "SGameplayTagChip.h"
#include "SGameplayTagPicker.h"
#include "Widgets/Input/SComboButton.h"

#define LOCTEXT_NAMESPACE "SQuestTagPicker"

void SQuestTagPicker::Construct(const FArguments& InArgs)
{
	TagAttribute = InArgs._Tag;
	OnTagChangedDelegate = InArgs._OnTagChanged;
	Filter = InArgs._Filter;
	bReadOnly = InArgs._ReadOnly;

	// SComboButton wrapping the chip — uses canonical SComboButton chrome to match the standard SGameplayTag-
	// Combo appearance adopters recognize. Critically, SComboButton drives its popup via FSlateApplication::
	// PushMenu which registers with FMenuStack — so child menus (right-click context menus inside the
	// SGameplayTagPicker dropdown) can resolve their parent. The prior SMenuAnchor wrapper didn't register
	// with FMenuStack and crashed the editor on right-click in the dropdown. The chip's prefix-strip text
	// behavior (via FQuestTagComposer::FormatTagForDisplay) is preserved through GetDisplayText.
	ChildSlot
	[
		SNew(SComboButton)
		.Cursor(EMouseCursor::Default)
		.OnGetMenuContent(this, &SQuestTagPicker::OnGetMenuContent)
		.ButtonContent()
		[
			SNew(SGameplayTagChip)
			.ReadOnly(bReadOnly)
			.ShowClearButton(true)
			.Text(this, &SQuestTagPicker::GetDisplayText)
			.ToolTipText(this, &SQuestTagPicker::GetTooltipText)
			// OnEditPressed intentionally unbound — SComboButton intercepts the click to drive the dropdown.
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
		.ShowMenuItems(true)        // Wraps the picker tree in FMenuBuilder context so right-click context
									// menus inside the dropdown have a registered parent in FMenuStack.
		.MaxHeight(350.0f)          // Standard SGameplayTagCombo's value — controls dropdown sizing via
									// the SBox HeightOverride that ShowMenuItems wrapping consults.
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

	// Close the SComboButton's popup. DismissAllMenus closes from the top of FMenuStack; typical case
	// has only this picker's menu open. Canonical close path for menus created via PushMenu (the path
	// SComboButton uses for its popup).
	FSlateApplication::Get().DismissAllMenus();
}

#undef LOCTEXT_NAMESPACE