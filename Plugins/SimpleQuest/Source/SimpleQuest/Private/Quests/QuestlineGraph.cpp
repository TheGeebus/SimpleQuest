// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Quests/QuestlineGraph.h"
#include "UObject/AssetRegistryTagsContext.h"

#if !WITH_EDITOR
#include "NativeGameplayTags.h"
#include "Utilities/QuestStateTagUtils.h"
#include "SimpleQuestLog.h"
#endif


void UQuestlineGraph::GetAssetRegistryTags(FAssetRegistryTagsContext Context) const
{
	UObject::GetAssetRegistryTags(Context);

	const FString EffectiveID = QuestlineID.IsEmpty() ? GetName() : QuestlineID;
	Context.AddTag(FAssetRegistryTag(TEXT("QuestlineEffectiveID"), EffectiveID, FAssetRegistryTag::TT_Alphabetical));

	// Publish FriendlyName so content-browser tooltips and similar surfaces can show it without loading the asset.
	// Empty when no FriendlyName is set — consumers fall back to the asset's short name.
	Context.AddTag(FAssetRegistryTag(TEXT("FriendlyName"), FriendlyName.ToString(), FAssetRegistryTag::TT_Alphabetical));

	if (!CompiledQuestTags.IsEmpty())
	{
		TArray<FString> TagStrings;
		TagStrings.Reserve(CompiledQuestTags.Num());
		for (const FName& TagName : CompiledQuestTags)
		{
			TagStrings.Add(TagName.ToString());
		}
		Context.AddTag(FAssetRegistryTag(TEXT("CompiledQuestTags"), FString::Join(TagStrings, TEXT("|")),	FAssetRegistryTag::TT_Hidden));
	}

	Context.AddTag(FAssetRegistryTag(TEXT("HasPendingRenames"), PendingTagRenames.Num() > 0 ? TEXT("true") : TEXT("false"), FAssetRegistryTag::TT_Hidden));
}

FText UQuestlineGraph::GetDisplayName() const
{
	if (!FriendlyName.IsEmpty())
	{
		return FriendlyName;
	}
	return FText::FromString(GetName());
}

void UQuestlineGraph::PostLoad()
{
	Super::PostLoad();
#if !WITH_EDITOR
	for (const FName& TagName : CompiledQuestTags)
	{
		auto Add = [this](FName InTagName)
		{
			RegisteredNativeTags.Add(MakeUnique<FNativeGameplayTag>(FName("SimpleQuest"), FName("SimpleQuest"),	InTagName, TEXT(""),
				ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD));
		};

		Add(TagName);

		const FString TagStr = TagName.ToString();
		if (!TagStr.StartsWith(FQuestStateTagUtils::Namespace)
			&& !TagStr.StartsWith(TEXT("SimpleQuest.QuestOutcome."))
			&& !TagStr.StartsWith(TEXT("Quest.Outcome.")))
		{
			Add(FQuestStateTagUtils::MakeStateFact(TagName, FQuestStateTagUtils::Leaf_Active));
			Add(FQuestStateTagUtils::MakeStateFact(TagName, FQuestStateTagUtils::Leaf_Completed));
			Add(FQuestStateTagUtils::MakeStateFact(TagName, FQuestStateTagUtils::Leaf_PendingGiver));
			Add(FQuestStateTagUtils::MakeStateFact(TagName, FQuestStateTagUtils::Leaf_Deactivated));
			Add(FQuestStateTagUtils::MakeStateFact(TagName, FQuestStateTagUtils::Leaf_Blocked));
		}
	}

	UE_LOG(LogSimpleQuest, Verbose, TEXT("UQuestlineGraph::PostLoad [%s] — registered %d native tag(s) "
		"(incl. state facts)"), *GetName(), RegisteredNativeTags.Num());
#endif
}
