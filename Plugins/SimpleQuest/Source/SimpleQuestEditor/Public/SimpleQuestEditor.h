// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ISimpleQuestEditorModule.h"
#include "Modules/ModuleInterface.h"
#include "NativeGameplayTags.h"

struct FGraphPanelPinConnectionFactory;
class FQuestlineGraphAssetTypeActions;
class FQuestlineGraphNodeFactory;

class FSimpleQuestEditor : public ISimpleQuestEditorModule
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	virtual void RegisterCompilerFactory(FQuestlineCompilerFactoryDelegate InFactory) override;
	virtual void UnregisterCompilerFactory() override;
	virtual TUniquePtr<FQuestlineGraphCompiler> CreateCompiler() const override;
	virtual void RegisterCompiledTags(const FString& GraphPath, const TArray<FName>& TagNames) override;

	TMap<FString, TArray<FName>> CompiledTagRegistry; // keyed by graph package path

private:
	TSharedPtr<FQuestlineGraphAssetTypeActions> QuestlineGraphAssetTypeActions;
	TSharedPtr<FQuestlineGraphNodeFactory> QuestlineGraphNodeFactory;
	TSharedPtr<FGraphPanelPinConnectionFactory> QuestlineConnectionFactory;

	FQuestlineCompilerFactoryDelegate CompilerFactory;
	
	TArray<TUniquePtr<FNativeGameplayTag>> CompiledNativeTags;

	void RegisterTagsFromAssetRegistry();
	void OnAssetRemoved(const FAssetData& AssetData);
	void WriteCompiledTagsIni() const;
	void RebuildNativeTags();

	bool bIsRegisteringTags = false;
};
