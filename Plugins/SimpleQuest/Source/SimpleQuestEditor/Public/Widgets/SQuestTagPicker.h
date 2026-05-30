// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "GameplayTagContainer.h"

class SWidget;

/**
 * Inline tag-picker widget for K2 graph nodes. Drop-in wrapper around SGameplayTagChip + SGameplayTagPicker
 * inside a standard SComboButton, with the chip's text routed through FQuestTagComposer::FormatTagForDisplay
 * so it paints the post-PluginPrefix form (e.g. "Questline.MyLine.Step1") rather than the full ToString()
 * ("SimpleQuest.Questline.MyLine.Step1"). Visual / behavioral parity with standard SGameplayTagCombo otherwise
 * — same chrome, same dropdown semantics, same right-click context menu via FMenuStack registration. The
 * underlying FGameplayTag value is unchanged; only chip-face display rendering shortens.
 */
class SIMPLEQUESTEDITOR_API SQuestTagPicker : public SCompoundWidget
{
public:
	DECLARE_DELEGATE_OneParam(FOnTagChanged, const FGameplayTag /*Tag*/)

	SLATE_BEGIN_ARGS(SQuestTagPicker)
		: _Filter()
		, _ReadOnly(false)
	{}
		/** Comma-delimited string of tag root names to filter the picker dropdown by. */
		SLATE_ARGUMENT(FString, Filter)

		/** Disables editing when true. */
		SLATE_ARGUMENT(bool, ReadOnly)

		/** Current tag value. */
		SLATE_ATTRIBUTE(FGameplayTag, Tag)

		/** Fired when the user picks a different tag (or clears the existing one). */
		SLATE_EVENT(FOnTagChanged, OnTagChanged)
	SLATE_END_ARGS();

	void Construct(const FArguments& InArgs);

private:
	FText GetDisplayText() const;
	FText GetTooltipText() const;
	FReply OnClearPressed();
	TSharedRef<SWidget> OnGetMenuContent();
	void OnPickerTagSelected(const TArray<FGameplayTagContainer>& TagContainers);

	TAttribute<FGameplayTag> TagAttribute;
	FOnTagChanged OnTagChangedDelegate;
	FString Filter;
	bool bReadOnly = false;
};