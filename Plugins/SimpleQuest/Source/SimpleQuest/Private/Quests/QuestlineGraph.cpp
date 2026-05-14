// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Quests/QuestlineGraph.h"

#include "GameplayTagContainer.h"
#include "UObject/AssetRegistryTagsContext.h"

#if !WITH_EDITOR
#include "NativeGameplayTags.h"
#include "Utilities/QuestTagComposer.h"
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
	
	// CompiledNodeAliases — pipe-separated list of "Contextual=Alias" pairs. Lets editor utilities discriminate cross-asset
	// inlinings from coincidental leaf-name matches when scanning the asset registry without loading each candidate asset.
	if (!CompiledNodeAliases.IsEmpty())
	{
		TArray<FString> PairStrings;
		PairStrings.Reserve(CompiledNodeAliases.Num());
		for (const FQuestCompiledNodeAlias& Pair : CompiledNodeAliases)
		{
			PairStrings.Add(FString::Printf(TEXT("%s=%s"), *Pair.ContextualFName.ToString(), *Pair.AliasFName.ToString()));
		}
		Context.AddTag(FAssetRegistryTag(TEXT("CompiledNodeAliases"), FString::Join(PairStrings, TEXT("|")), FAssetRegistryTag::TT_Hidden));
	}

	Context.AddTag(FAssetRegistryTag(TEXT("HasPendingRenames"), PendingTagRenames.Num() > 0 ? TEXT("true") : TEXT("false"), FAssetRegistryTag::TT_Hidden));

	// ListenerGroupTags + OutwardSetterGroupTags drive the manager's reachability-walked async-load. Manager builds
	// an inverted GroupTag→graphs index from ListenerGroupTags at startup; when a graph registers, the manager walks
	// the graph's OutwardSetterGroupTags and async-loads matching listener graphs. Pipe-separated tag-name lists,
	// same shape as CompiledQuestTags.
	if (!ListenerGroupTags.IsEmpty())
	{
		TArray<FString> TagStrings;
		TagStrings.Reserve(ListenerGroupTags.Num());
		for (const FGameplayTag& Tag : ListenerGroupTags)
		{
			TagStrings.Add(Tag.GetTagName().ToString());
		}
		Context.AddTag(FAssetRegistryTag(TEXT("ListenerGroupTags"), FString::Join(TagStrings, TEXT("|")), FAssetRegistryTag::TT_Hidden));
	}

	if (!OutwardSetterGroupTags.IsEmpty())
	{
		TArray<FString> TagStrings;
		TagStrings.Reserve(OutwardSetterGroupTags.Num());
		for (const FGameplayTag& Tag : OutwardSetterGroupTags)
		{
			TagStrings.Add(Tag.GetTagName().ToString());
		}
		Context.AddTag(FAssetRegistryTag(TEXT("OutwardSetterGroupTags"), FString::Join(TagStrings, TEXT("|")), FAssetRegistryTag::TT_Hidden));
	}
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

		if (FQuestTagComposer::IsIdentityTag(TagName))
		{
			for (EQuestStateLeaf Leaf : FQuestTagComposer::AllStateLeaves)
			{
				Add(FQuestTagComposer::MakeStateFact(TagName, Leaf));
			}
		}
	}

	UE_LOG(LogSimpleQuest, Verbose, TEXT("UQuestlineGraph::PostLoad [%s] — registered %d native tag(s) "
		"(incl. state facts)"), *GetName(), RegisteredNativeTags.Num());
#endif
}
