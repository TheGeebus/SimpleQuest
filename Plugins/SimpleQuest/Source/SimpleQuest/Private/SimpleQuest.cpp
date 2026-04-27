// Copyright Epic Games, Inc. All Rights Reserved.

#include "SimpleQuest.h"
#include "GameplayTagsManager.h"
#include "SimpleQuestLog.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Quests/QuestlineGraph.h"
#include "Utilities/QuestStateTagUtils.h"

#define LOCTEXT_NAMESPACE "FSimpleQuestModule"

DEFINE_LOG_CATEGORY(LogSimpleQuest);

void FSimpleQuest::StartupModule()
{
	UGameplayTagsManager::OnLastChanceToAddNativeTags().AddStatic(&FSimpleQuest::RegisterCompiledQuestTags);
}

void FSimpleQuest::RegisterCompiledQuestTags()
{
    IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

    FARFilter Filter;
    Filter.ClassPaths.Add(UQuestlineGraph::StaticClass()->GetClassPathName());
    Filter.bRecursiveClasses = true;

    TArray<FAssetData> Assets;
    AR.GetAssets(Filter, Assets);

    UGameplayTagsManager& TagsManager = UGameplayTagsManager::Get();
    int32 TotalTags = 0;

    for (const FAssetData& Asset : Assets)
    {
        FAssetTagValueRef TagValue = Asset.TagsAndValues.FindTag(TEXT("CompiledQuestTags"));
        if (!TagValue.IsSet() || TagValue.GetValue().IsEmpty())
        {
            continue;
        }

        TArray<FString> TagStrings;
        TagValue.GetValue().ParseIntoArray(TagStrings, TEXT("|"), true);

        for (const FString& TagStr : TagStrings)
        {
            const FName QuestTag(*TagStr);
            TagsManager.AddNativeGameplayTag(QuestTag);
            ++TotalTags;

            // Expand state facts for tags not already in a state or outcome namespace
            if (!TagStr.StartsWith(FQuestStateTagUtils::Namespace)
                && !TagStr.StartsWith(TEXT("SimpleQuest.QuestOutcome.")))
            {
                TagsManager.AddNativeGameplayTag(FQuestStateTagUtils::MakeStateFact(QuestTag, FQuestStateTagUtils::Leaf_Live));
                TagsManager.AddNativeGameplayTag(FQuestStateTagUtils::MakeStateFact(QuestTag, FQuestStateTagUtils::Leaf_Completed));
                TagsManager.AddNativeGameplayTag(FQuestStateTagUtils::MakeStateFact(QuestTag, FQuestStateTagUtils::Leaf_PendingGiver));
                TagsManager.AddNativeGameplayTag(FQuestStateTagUtils::MakeStateFact(QuestTag, FQuestStateTagUtils::Leaf_Deactivated));
                TagsManager.AddNativeGameplayTag(FQuestStateTagUtils::MakeStateFact(QuestTag, FQuestStateTagUtils::Leaf_Blocked));
                TotalTags += 5;
            }
        }
    }

    UE_LOG(LogSimpleQuest, Log, TEXT("RegisterCompiledQuestTags — registered %d tag(s) "
        "(incl. state facts) from %d questline asset(s)"), TotalTags, Assets.Num());
}
void FSimpleQuest::ShutdownModule()
{

}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FSimpleQuest, SimpleQuest)