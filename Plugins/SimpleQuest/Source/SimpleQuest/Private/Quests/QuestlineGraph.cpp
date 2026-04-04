// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Quests/QuestlineGraph.h"
#include "UObject/AssetRegistryTagsContext.h"

#if !WITH_EDITOR
#include "NativeGameplayTags.h"
#endif

void UQuestlineGraph::GetAssetRegistryTags(FAssetRegistryTagsContext Context) const
{
	UObject::GetAssetRegistryTags(Context);
	const FString EffectiveID = QuestlineID.IsEmpty() ? GetName() : QuestlineID;
	Context.AddTag(FAssetRegistryTag(TEXT("QuestlineEffectiveID"),	EffectiveID, FAssetRegistryTag::TT_Alphabetical));
}

void UQuestlineGraph::PostLoad()
{
	Super::PostLoad();
#if !WITH_EDITOR
	for (const FName& TagName : CompiledQuestTags)
	{
		RegisteredNativeTags.Add(MakeUnique<FNativeGameplayTag>(FName("SimpleQuest"), FName("SimpleQuest"), TagName, TEXT(""), ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD));
	}
#endif
}
