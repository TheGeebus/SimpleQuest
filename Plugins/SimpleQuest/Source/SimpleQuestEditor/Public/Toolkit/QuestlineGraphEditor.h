// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Toolkits/AssetEditorToolkit.h"

class UQuestlineGraph;
class SGraphEditor;

class FQuestlineGraphEditor : public FAssetEditorToolkit
{
public:
	void InitQuestlineGraphEditor(
		const EToolkitMode::Type Mode,
		const TSharedPtr<IToolkitHost>& InitToolkitHost,
		UQuestlineGraph* InQuestlineGraph);

	// FAssetEditorToolkit interface
	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;
	virtual FString GetWorldCentricTabPrefix() const override;
	virtual FLinearColor GetWorldCentricTabColorScale() const override;
	virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;
	virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;

private:
	TSharedRef<SDockTab> SpawnGraphViewportTab(const FSpawnTabArgs& Args);
	TSharedRef<SGraphEditor> CreateGraphEditorWidget();

	TObjectPtr<UQuestlineGraph> QuestlineGraph;
	TSharedPtr<SGraphEditor> GraphEditorWidget;

	static const FName GraphViewportTabId;
};
