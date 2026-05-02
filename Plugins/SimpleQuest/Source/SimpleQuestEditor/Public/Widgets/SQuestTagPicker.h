// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "GameplayTagContainer.h"

class SMenuAnchor;
class SWidget;

/**
 * Inline tag-picker widget for K2 graph nodes. Drop-in alternative to SGameplayTagCombo at SimpleQuest call
 * sites that want short-form chip display without the surrounding combo-button frame. Wraps SGameplayTagChip
 * + SGameplayTagPicker via an SMenuAnchor (no button-frame intermediary), so the rendered footprint is
 * exactly chip-width — no padding gutters between the chip and a wrapping field. Chip text routes through
 * FQuestTagComposer::FormatTagForDisplay so it paints the post-PluginPrefix form rather than the full
 * ToString(). The underlying FGameplayTag is unchanged; only display rendering shortens.
 *
 * SGameplayTagCombo stays in use elsewhere (Details-panel customization, etc.) — this widget is specifically
 * for the inline-on-node case where width and click semantics matter.
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
	FReply OnChipPressed();
	FReply OnClearPressed();
	TSharedRef<SWidget> OnGetMenuContent();
	void OnPickerTagSelected(const TArray<FGameplayTagContainer>& TagContainers);

	TAttribute<FGameplayTag> TagAttribute;
	FOnTagChanged OnTagChangedDelegate;
	FString Filter;
	bool bReadOnly = false;

	TSharedPtr<SMenuAnchor> MenuAnchor;
};