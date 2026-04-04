// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ISimpleQuestEditorModule.h"
#include "NativeGameplayTags.h"
#include "Modules/ModuleInterface.h"

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

	TMap<FString, TArray<TUniquePtr<FNativeGameplayTag>>> CompiledTagRegistry; // keyed by graph package path

private:
	TSharedPtr<FQuestlineGraphAssetTypeActions> QuestlineGraphAssetTypeActions;
	TSharedPtr<FQuestlineGraphNodeFactory> QuestlineGraphNodeFactory;
	TSharedPtr<FGraphPanelPinConnectionFactory> QuestlineConnectionFactory;

	FQuestlineCompilerFactoryDelegate CompilerFactory;

	void OnMapChanged(uint32 MapChangeEventFlag);
	void OnPreBeginPIE(const bool bIsSimulating);
	
};
