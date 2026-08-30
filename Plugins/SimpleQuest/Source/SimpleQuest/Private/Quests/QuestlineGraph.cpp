// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Quests/QuestlineGraph.h"

#include "GameplayTagContainer.h"
#include "SimpleQuestLog.h"
#include "Display/QuestDisplayData.h"
#include "Quests/QuestNodeBase.h"
#include "UObject/AssetRegistryTagsContext.h"
#include "Utilities/QuestTagComposer.h"
#if !WITH_EDITOR
#include "NativeGameplayTags.h"
#include "SimpleQuestLog.h"
#endif


void UQuestlineGraph::GetAssetRegistryTags(FAssetRegistryTagsContext Context) const
{
	UObject::GetAssetRegistryTags(Context);

	const FString EffectiveID = GetEffectiveID();
	Context.AddTag(FAssetRegistryTag(TEXT("QuestlineEffectiveID"), EffectiveID, FAssetRegistryTag::TT_Alphabetical));

	// Publish DisplayName so content-browser tooltips and similar surfaces can show it without loading the asset.
	// Empty when no DisplayName is set - consumers fall back to the asset's short name.
	Context.AddTag(FAssetRegistryTag(TEXT("DisplayName"), DisplayName.ToString(), FAssetRegistryTag::TT_Alphabetical));

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
	
	// CompiledNodeAliases - pipe-separated list of "Contextual=Alias" pairs. Lets editor utilities discriminate cross-asset
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

#if WITH_EDITOR
TArray<FString> UQuestlineGraph::GetCompiledDisplayRecords() const
{
	auto HasPayload = [](const FText& Name, const FText& Desc, const UQuestDisplayData* Data)
	{ return !Name.IsEmpty() || !Desc.IsEmpty() || Data != nullptr; };

	auto FormatRecord = [](const FString& TagString, const FText& Name, const FText& Desc, const UQuestDisplayData* Data)
	{
		// FText export keeps the package namespace (bStripPackageNamespace = false), so it round-trips losslessly and
		// stays the single point to revisit if cooked-style namespace stripping is ever needed for localized builds.
		// The reader parses whatever form is written here, so this one setting governs the whole round-trip.
		FString NameExport, DescExport;
		FTextStringHelper::WriteToBuffer(NameExport, Name, true, false);
		FTextStringHelper::WriteToBuffer(DescExport, Desc, true, false);
        const FString DataPath = Data ? Data->GetPathName() : FString();
		return FString::Printf(TEXT("%s=%s|%s|%s"), *TagString, *NameExport, *DescExport, *DataPath);
	};

	TArray<FString> Records;

	// Questline-self: the standalone identity tag carries the graph's own title/blurb/art. RAW authored name - empty
	// means "no title," which the UI honors - not the asset-name fallback GetDisplayName() applies.
	if (HasPayload(GetAuthoredDisplayName(), Description, DisplayData))
	{
		Records.Add(FormatRecord(FQuestTagComposer::IdentityNamespace + GetEffectiveID(), GetAuthoredDisplayName(), Description, DisplayData));
	}

	// Every compiled node with a display payload, under its contextual tag (all nodes, not just containers).
	for (const TPair<FName, TObjectPtr<UQuestNodeBase>>& Pair : CompiledNodes)
	{
		const UQuestNodeBase* Node = Pair.Value;
		if (!Node || !Node->GetContextualTag().IsValid()) continue;
		if (!HasPayload(Node->GetDisplayName(), Node->GetDescription(), Node->GetDisplayData())) continue;
		Records.Add(FormatRecord(Node->GetContextualTag().ToString(), Node->GetDisplayName(), Node->GetDescription(), Node->GetDisplayData()));
	}

	Records.Sort();   // deterministic line order → stable, scoped diffs
	return Records;
}

void UQuestlineGraph::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UQuestlineGraph, QuestlineID))
	{
		// Trim surrounding whitespace so a stray space can never become the identity. This is more than tidiness: a
		// whitespace-only value is not IsEmpty(), so GetEffectiveID() returns it instead of falling back to the asset name,
		// and it then sanitizes away to nothing - which composes the tag "SimpleQuest.Questline." (rejected by the engine,
		// silently replaced with the bare root) and resolves the export folder to the export root. Trimming converts that
		// case into the plain empty value the asset-name fallback already handles correctly.
		SetQuestlineID(QuestlineID);
	}
}
#endif

FText UQuestlineGraph::GetDisplayName() const
{
	if (!DisplayName.IsEmpty())
	{
		return DisplayName;
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

	UE_LOG(LogSimpleQuestCompiler, Verbose, TEXT("UQuestlineGraph::PostLoad [%s] - registered %d native tag(s) "
		"(incl. state facts)"), *GetName(), RegisteredNativeTags.Num());
#endif
}

void UQuestlineGraph::PostDuplicate(bool bDuplicateForPIE)
{
	Super::PostDuplicate(bDuplicateForPIE);
	if (bDuplicateForPIE) { return; }

	// A COPY OF A COMPILED QUESTLINE IS NOT A COMPILED QUESTLINE. Every compiled field is a plain UPROPERTY and the node
	// instances are outered to this asset, so duplication carries the entire model onto the copy - still keyed by the
	// SOURCE's tags. Nothing downstream can tell the two apart: the manager keys its instance registry by tag and skips
	// whichever graph registers second, so one of the two questlines goes silently inert; and starting the copy resolves
	// the original's entry tags and drives the original's nodes.
	//
	// CLEARED rather than recompiled. Uncompiled is a state every consumer already handles - the asset registry skips
	// empty arrays, editor tag registration has a not-yet-compiled branch, the toolkit reports the compile status as
	// unknown. A wrong compiled model is a state nothing handles, because everything downstream trusts it.
	const bool bHadCompiledModel = CompiledQuestTags.Num() > 0 || CompiledNodes.Num() > 0;

	CompiledQuestTags.Reset();
	CompiledNodeAliases.Reset();
	EntryNodeTags.Reset();
	CompiledNodes.Reset();
	CompiledQuestlineRewards.Reset();
	ListenerGroupTags.Reset();
	OutwardSetterGroupTags.Reset();
#if WITH_EDITORONLY_DATA
	// Transient keeps this out of the SAVE, not out of a DUPLICATE - only DuplicateTransient would do that. It comes
	// across pointing at whichever nodes the copy remapped to, which a fresh compile has to repopulate regardless.
	CompiledEditorNodes.Reset();
#endif

	if (bHadCompiledModel)
	{
		UE_LOG(LogSimpleQuestCompiler, Display,
			   TEXT("UQuestlineGraph::PostDuplicate [%s] - cleared the compiled model inherited from its source. Recompile before use."),
			   *GetName());
	}

	// QuestlineID is deliberately NOT cleared: it is authored identity, and silently dropping it is a worse surprise
	// than the collision. But an explicit ID arriving on a copy IS a guaranteed tag-namespace clash on the next compile,
	// so it gets named here rather than discovered as a duplicate-tag rejection later.
	if (!QuestlineID.IsEmpty())
	{
		UE_LOG(LogSimpleQuestCompiler, Warning,
			   TEXT("UQuestlineGraph::PostDuplicate [%s] - inherited QuestlineID '%s' from its source. Two assets sharing a "
					"QuestlineID compile into one tag namespace; change one before compiling."),
			   *GetName(), *QuestlineID);
	}
}

FGameplayTag UQuestlineGraph::GetIdentityTag() const
{
	// An unregistered tag is a real answer rather than an error - an uncompiled asset has no identity to give, and every
	// caller branches on validity instead of assuming one exists.
	return CompiledIdentityTag.IsNone() ? FGameplayTag() : FGameplayTag::RequestGameplayTag(CompiledIdentityTag, false);
}

void UQuestlineGraph::SetQuestlineID(const FString& InQuestlineID)
{
	QuestlineID = InQuestlineID.TrimStartAndEnd();
}

