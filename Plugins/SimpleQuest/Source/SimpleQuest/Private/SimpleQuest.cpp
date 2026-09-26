// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "SimpleQuest.h"
#include "Delegates/DelegateCombinations.h"
#include "GameplayTagsManager.h"
#include "SimpleQuestLog.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/EngineVersionComparison.h"
#include "Quests/QuestlineGraph.h"
#include "Settings/SimpleQuestSettings.h"
#include "Utilities/QuestTagComposer.h"

#define LOCTEXT_NAMESPACE "FSimpleQuestModule"

DEFINE_LOG_CATEGORY(LogSimpleQuest);
DEFINE_LOG_CATEGORY(LogSimpleQuestActivation);
DEFINE_LOG_CATEGORY(LogSimpleQuestCompiler);
DEFINE_LOG_CATEGORY(LogSimpleQuestResolver);
DEFINE_LOG_CATEGORY(LogSimpleQuestSubscription);
DEFINE_LOG_CATEGORY(LogSimpleQuestState);

void FSimpleQuest::StartupModule()
{
    // 5.8 deprecated OnLastChanceToAddNativeTags and names two replacements, neither of which is a drop-in here.
    // CallOrRegister_OnAddNativeTagsDelegate does not EXIST before 5.8, so it cannot be used unguarded on a plugin that
    // supports three engines. "Call AddNativeGameplayTag directly" is worse than it sounds for THIS caller: registration
    // reads the asset registry, and at module startup that scan has not necessarily run, so calling straight through
    // would register nothing and say so only in a log line reading "0 tag(s) from 0 questline asset(s)".
    // So: the sanctioned call on 5.8, the old one untouched on 5.6 and 5.7, and identical behavior on both paths.
#if UE_VERSION_OLDER_THAN(5, 8, 0)
    UGameplayTagsManager::OnLastChanceToAddNativeTags().AddStatic(&FSimpleQuest::RegisterCompiledQuestTags);
    UGameplayTagsManager::OnLastChanceToAddNativeTags().AddStatic(&FSimpleQuest::RegisterAuthoredQuestTags);
#else
    UGameplayTagsManager::CallOrRegister_OnAddNativeTagsDelegate(FSimpleMulticastDelegate::FDelegate::CreateStatic(&FSimpleQuest::RegisterCompiledQuestTags));
    UGameplayTagsManager::CallOrRegister_OnAddNativeTagsDelegate(FSimpleMulticastDelegate::FDelegate::CreateStatic(&FSimpleQuest::RegisterAuthoredQuestTags));
#endif

    // Apply log verbosity from Project Settings. UDeveloperSettings's Config flow loads the values during
    // engine boot before module startup, so GetDefault here returns settings already populated from
    // DefaultSimpleQuest.ini. Replaces the need for designers to edit DefaultEngine.ini's [Core.Log] section
    // by hand — Project Settings → Plugins → Simple Quest → Logging is the single source.
    GetDefault<USimpleQuestSettings>()->ApplyLogVerbosity();
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

void FSimpleQuest::RegisterAuthoredQuestTags()
{
    const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("SimpleQuest"));
    if (!Plugin.IsValid()) return;

    const FString IniPath = Plugin->GetBaseDir() / TEXT("Config/Tags/SimpleQuestAuthoredTags.ini");

    FString Content;
    if (!FFileHelper::LoadFileToString(Content, *IniPath))
    {
        UE_LOG(LogSimpleQuest, Warning, TEXT("RegisterAuthoredQuestTags — file not found: %s"), *IniPath);
        return;
    }

    UGameplayTagsManager& Manager = UGameplayTagsManager::Get();
    TArray<FString> Lines;
    Content.ParseIntoArrayLines(Lines);

    int32 Count = 0;
    for (const FString& Line : Lines)
    {
        const int32 TagStart = Line.Find(TEXT("Tag=\""));
        if (TagStart == INDEX_NONE) continue;
        const int32 ValueStart = TagStart + 5;
        const int32 ValueEnd = Line.Find(TEXT("\""), ESearchCase::IgnoreCase, ESearchDir::FromStart, ValueStart);
        if (ValueEnd == INDEX_NONE) continue;

        const FName TagName(*Line.Mid(ValueStart, ValueEnd - ValueStart));
        Manager.AddNativeGameplayTag(TagName);
        ++Count;
    }

    UE_LOG(LogSimpleQuest, Display, TEXT("RegisterAuthoredQuestTags — registered %d tag(s) from %s"), Count, *IniPath);
}

void FSimpleQuest::ShutdownModule()
{

}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FSimpleQuest, SimpleQuest)