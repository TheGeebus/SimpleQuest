// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "UObject/WeakObjectPtrTemplates.h"

struct FGameplayTag;
class UQuestlineGraph;
class IDetailLayoutBuilder;
class IPropertyUtilities;
class SWidget;

/**
 * Custom Details layout for UQuestlineGraph's QuestlineRewards map. Replaces the raw nested-map editor (four levels of
 * disclosure) with one flat row per authored outcome, plus an "Add Reward Outcome" combo whose menu is built live from the
 * graph's top-level Exit outcomes MINUS already-keyed ones, PLUS Any Outcome — so a designer can only key rewards on
 * outcomes the questline actually produces (no typos, no duplicates), matching the compile-time drift validation. Adds are
 * a direct, transacted mutation of QuestlineRewards (the engine map handle's AddItem can't add a chosen key); each row's
 * reward array is still edited through the property-handle system so Undo/Redo + PostEditChangeProperty fire naturally.
 */
class FQuestlineGraphRewardsDetailsCustomization : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:
	TWeakObjectPtr<UQuestlineGraph> CustomizedGraph;
	TWeakPtr<IPropertyUtilities> PropertyUtilities;

	/** Gather this graph's top-level Exit outcome tags (valid ones), + the reserved Any-Outcome tag, minus keys already
	 *  in QuestlineRewards — the valid choices for a new reward outcome. */
	TArray<FGameplayTag> GetAvailableOutcomes() const;
	bool IsOutcomeStale(const FGameplayTag& OutcomeKey) const;

	TSharedRef<SWidget> BuildAddOutcomeMenu();
	void OnAddOutcome(FGameplayTag Outcome);
	void OnRemoveOutcome(FGameplayTag Outcome);
	void OnRekeyOutcome(FGameplayTag OldOutcome, FGameplayTag NewOutcome);
	TSharedRef<SWidget> BuildRekeyMenu(FGameplayTag OldOutcome);
	bool HasParkedEntry() const;
	void OnClearOutcome(FGameplayTag Outcome);
	TSharedRef<SWidget> MakeIconButton(const FSlateBrush* Icon, const FText& Tooltip, bool bIsComboMenu, FGameplayTag OutcomeKey);
	void ForceRefresh();
};