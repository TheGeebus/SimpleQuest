// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "AssetTypeActions_Base.h"

/**
 * Browser identity for a Questline Graph. Keeps its own class rather than using FQuestAssetTypeActions because it
 * answers a question the data assets do not: it opens a custom editor.
 *
 * Takes its category rather than naming one, so the module registers SimpleQuest's category once and every asset
 * files under the same bit - the alternative is a literal here that silently disagrees with the others the day one
 * of them changes.
 */
class FQuestlineGraphAssetTypeActions : public FAssetTypeActions_Base
{
public:
	explicit FQuestlineGraphAssetTypeActions(uint32 InCategory) : Category(InCategory) {}

	virtual FText GetName() const override;
	virtual FColor GetTypeColor() const override;
	virtual UClass* GetSupportedClass() const override;
	virtual uint32 GetCategories() override;
	virtual void OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> EditWithinLevelEditor) override;
	virtual FText GetAssetDescription(const FAssetData& AssetData) const override;	

private:
	uint32 Category = 0;
};
