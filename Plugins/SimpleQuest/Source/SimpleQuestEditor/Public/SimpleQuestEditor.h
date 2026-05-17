// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "ISimpleQuestEditorModule.h"
#include "Modules/ModuleInterface.h"
#include "NativeGameplayTags.h"

struct FGraphPanelPinFactory;
struct FGraphPanelPinConnectionFactory;
class FQuestlineGraphAssetTypeActions;
class FQuestlineGraphNodeFactory;
class FQuestPIEDebugChannel;
class FSlateStyleSet;
class FSpawnTabArgs;
class SDockTab;

class FSimpleQuestEditor : public ISimpleQuestEditorModule
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	virtual void RegisterCompilerFactory(FQuestlineCompilerFactoryDelegate InFactory) override;
	virtual void UnregisterCompilerFactory() override;
	virtual TUniquePtr<FQuestlineGraphCompiler> CreateCompiler() const override;
	virtual void RegisterCompiledTags(const FString& GraphPath, const TArray<FName>& TagNames) override;
	virtual void BeginCompileBatch() override;
	virtual void EndCompileBatch() override;
	TMap<FString, TArray<FName>> CompiledTagRegistry; // keyed by graph package path
	
	virtual void CompileAllQuestlineGraphs() override;
	virtual void CollectLinkedNeighborhood(UQuestlineGraph* Primary, TArray<UQuestlineGraph*>& OutNeighborhood) const override;
	
	FOnQuestlineCompiled QuestlineCompiledDelegate;
	virtual FOnQuestlineCompiled& OnQuestlineCompiled() override { return QuestlineCompiledDelegate; }

	/** Editor-side PIE debug channel. Lifetime managed by this module. Access via GetPIEDebugChannel(). */
	static FQuestPIEDebugChannel* GetPIEDebugChannel();

private:
	TSharedPtr<FQuestlineGraphAssetTypeActions> QuestlineGraphAssetTypeActions;
	TSharedPtr<FQuestlineGraphNodeFactory> QuestlineGraphNodeFactory;
	TSharedPtr<FGraphPanelPinConnectionFactory> QuestlineConnectionFactory;
	
	TSharedPtr<FSlateStyleSet> StyleSet;
	
	FQuestlineCompilerFactoryDelegate CompilerFactory;
	
	TArray<TUniquePtr<FNativeGameplayTag>> CompiledNativeTags;

	TUniquePtr<FQuestPIEDebugChannel> PIEDebugChannel;

	void LoadCompiledTagsFromIni();
	void MigrateLegacyTagsIni();
	static FString GetCompiledTagsIniPath();
	
	void RegisterTagsFromAssetRegistry();
	void OnAssetRemoved(const FAssetData& AssetData);
	void WriteCompiledTagsIni() const;
	void RebuildNativeTags(bool bRefreshTree = false);
	
	/**
	 * Bound to FCoreUObjectDelegates::OnAssetLoaded in StartupModule. When a UBlueprint asset loads, scans its CDO for top-level
	 * FGameplayTag / FGameplayTagContainer UPROPERTYs whose stored FName matches a NewTagName on the active redirect map. A match
	 * indicates the field MAY have been transparently rewritten by FGameplayTag::PostSerialize during deserialization — the asset's
	 * disk bytes still reference the OldTagName, and UE doesn't mark the asset dirty for that hidden rewrite. Marking dirty here
	 * surfaces the asset in the Content Browser so Save All persists the healed value and the redirect can later be cleaned up
	 * without orphaning unloaded-at-rename-time assets. Heuristic accepts false positives — an asset whose FGameplayTag was already
	 * at the redirect's NewTagName on disk gets a no-op save, which UE handles cleanly.
	 */
	void MarkDirtyOnRedirectedTagLoad(UObject* LoadedAsset);
	FDelegateHandle OnAssetLoadedHandle;
	
	bool bIsRegisteringTags = false;

	TSharedRef<SDockTab> SpawnStaleQuestTagsTab(const FSpawnTabArgs& Args);

	static const FName StaleQuestTagsTabId;

	/**
	 * Incremental: registers FNativeGameplayTags for any names in TagNames not already in the
	 * current native-tag set. Each FNativeGameplayTag ctor calls AddNativeGameplayTag → the tag
	 * becomes findable via RequestGameplayTag immediately, without needing a tree rebuild.
	 * Tree rebuild is deferred to EndCompileBatch (or RebuildNativeTags's full path).
	 *
	 * Shared between RebuildNativeTags's full rebuild loop and the per-graph batch path so the
	 * state-fact expansion logic stays in one place.
	 */
	void AddNativeTagsForGraph(const TArray<FName>& TagNames);
	int32 NumSkippedAlreadyRegistered = 0;  // TEMP
	int32 NumConstructedFresh = 0;          // TEMP
	/**
	 * Parallel index for AddNativeTagsForGraph's O(1) "already registered?" check. Stays in sync
	 * with CompiledNativeTags. Reset() in lockstep with the array.
	 */
	TSet<FName> CompiledNativeTagNames;

	bool bBatchActive = false;
	bool bBatchHasStaleTags = false;
};
