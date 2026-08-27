// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "AssetTypeActions_Base.h"

/**
 * Browser identity for a SimpleQuest asset type - its name, its colour, and the category it files under. One
 * parameterized class rather than one subclass per asset: every type wants the same three answers and differs only in
 * their values, so subclassing five times would be five places for the category to drift apart.
 *
 * Questline Graph keeps its own subclass because it answers a fourth question - it opens a custom editor.
 */
class FQuestAssetTypeActions : public FAssetTypeActions_Base
{
public:
	FQuestAssetTypeActions(UClass* InSupportedClass, FText InName, FColor InColor, uint32 InCategory)
		: SupportedClass(InSupportedClass), Name(MoveTemp(InName)), Color(InColor), Category(InCategory)
	{
	}

	virtual FText GetName() const override { return Name; }
	virtual FColor GetTypeColor() const override { return Color; }
	virtual UClass* GetSupportedClass() const override { return SupportedClass; }
	virtual uint32 GetCategories() override { return Category; }

private:
	UClass* SupportedClass = nullptr;
	FText Name;
	FColor Color = FColor::White;
	uint32 Category = 0;
};

