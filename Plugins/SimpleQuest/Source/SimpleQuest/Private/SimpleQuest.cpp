// Copyright Epic Games, Inc. All Rights Reserved.

#include "SimpleQuest.h"
#include "Delegates/DelegateCombinations.h"
#include "GameplayTagsManager.h"
#include "SimpleQuestLog.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Quests/QuestlineGraph.h"
#include "Utilities/QuestTagComposer.h"

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
            if (FQuestTagComposer::IsIdentityTag(QuestTag))
            {
                for (EQuestStateLeaf Leaf : FQuestTagComposer::AllStateLeaves)
                {
                    TagsManager.AddNativeGameplayTag(FQuestTagComposer::MakeStateFact(QuestTag, Leaf));
                    ++TotalTags;
                }
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