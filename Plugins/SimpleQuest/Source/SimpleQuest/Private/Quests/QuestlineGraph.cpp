// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Quests/QuestlineGraph.h"
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

	// Drives the manager's startup auto-load scan: any asset flagged true here gets pre-registered (without firing
	// its Start node) so its UActivationGroupListenerNode instances can subscribe to their group signal channels at
	// game start, regardless of whether the asset is in InitialQuestlines. Stamped by the compiler post-registration.
	Context.AddTag(FAssetRegistryTag(TEXT("HasActivationGroupListener"), bHasActivationGroupListener ? TEXT("true") : TEXT("false"), FAssetRegistryTag::TT_Hidden));
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
