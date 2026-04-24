// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "AssetTypes/QuestlineGraphAssetTypeActions.h"
#include "Quests/QuestlineGraph.h"
#include "AssetTypeCategories.h"
#include "Toolkit/QuestlineGraphEditor.h"

FText FQuestlineGraphAssetTypeActions::GetName() const
{
	return NSLOCTEXT("SimpleQuestEditor", "QuestlineGraphAssetName", "Questline Graph");
}

FColor FQuestlineGraphAssetTypeActions::GetTypeColor() const
{
	return FColor(45, 170, 130);
}

UClass* FQuestlineGraphAssetTypeActions::GetSupportedClass() const
{
	return UQuestlineGraph::StaticClass();
}

uint32 FQuestlineGraphAssetTypeActions::GetCategories()
{
	return EAssetTypeCategories::Gameplay;
}

void FQuestlineGraphAssetTypeActions::OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> EditWithinLevelEditor)
{
	const EToolkitMode::Type Mode = EditWithinLevelEditor.IsValid()
		? EToolkitMode::WorldCentric
		: EToolkitMode::Standalone;

	for (UObject* Object : InObjects)
	{
		if (UQuestlineGraph* QuestlineGraph = Cast<UQuestlineGraph>(Object))
		{
			TSharedRef<FQuestlineGraphEditor> NewEditor = MakeShared<FQuestlineGraphEditor>();
			NewEditor->InitQuestlineGraphEditor(Mode, EditWithinLevelEditor, QuestlineGraph);
		}
	}
}

FText FQuestlineGraphAssetTypeActions::GetAssetDescription(const FAssetData& AssetData) const
{
	// Read the FriendlyName published by UQuestlineGraph::GetAssetRegistryTags — no load needed. When empty,
	// fall through to default behavior (empty description, which the asset browser handles as "no description").
	FAssetTagValueRef FriendlyNameTag = AssetData.TagsAndValues.FindTag(TEXT("FriendlyName"));
	if (FriendlyNameTag.IsSet() && !FriendlyNameTag.GetValue().IsEmpty())
	{
		return FText::FromString(FriendlyNameTag.GetValue());
	}
	return FText::GetEmpty();
}
