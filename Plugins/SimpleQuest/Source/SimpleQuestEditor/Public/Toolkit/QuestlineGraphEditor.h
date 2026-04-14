// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailsView.h"
#include "Toolkit/QuestlineBreadcrumbBar.h"



class SQuestlineGraphPanel;
struct FQuestlineOutlinerItem;
class SQuestlineOutlinerPanel;
class UQuestlineGraph;
class SGraphEditor;

class FQuestlineGraphEditor : public FAssetEditorToolkit
{
public:
	virtual ~FQuestlineGraphEditor() override;
	void InitQuestlineGraphEditor(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UQuestlineGraph* InQuestlineGraph);

	// FAssetEditorToolkit interface
	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;
	virtual FString GetWorldCentricTabPrefix() const override;
	virtual FLinearColor GetWorldCentricTabColorScale() const override;
	virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;
	virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;
	
	struct FEdNodeLocation
	{
		UEdGraph* HostGraph = nullptr;
		UEdGraphNode* EdNode = nullptr;
		bool IsValid() const { return HostGraph && EdNode; }
	};

private:
	TSharedRef<SDockTab> SpawnGraphViewportTab(const FSpawnTabArgs& Args);
	SGraphEditor::FGraphEditorEvents MakeGraphEvents();
	TSharedRef<SQuestlineGraphPanel> CreateGraphEditorWidget();
	void BindGraphCommands();
	void DeleteSelectedNodes();

	// Compile and save graph data layer
	void CompileQuestlineGraph();
	virtual void SaveAsset_Execute() override;
	void ExtendToolbar();
	void FillToolbar(FToolBarBuilder& ToolbarBuilder);

	FText GetGraphDisplayName(UEdGraph* Graph) const;

	enum class EQuestlineCompileStatus : uint8 { Unknown, UpToDate, Error };

	void OnGraphChanged(const FEdGraphEditAction& Action);
	FSlateIcon GetCompileStatusIcon() const;
	
	TSharedRef<SWidget> GenerateCompileOptionsMenu();

	void OnExternalCompile(const FString& PackagePath, bool bSuccess);

	/** Notifies all graphs in the questline (top-level + inner) to rebuild node widgets. */
	void RefreshAllNodeWidgets();
	
	FDelegateHandle ExternalCompileHandle;

	EQuestlineCompileStatus CompileStatus = EQuestlineCompileStatus::Unknown;

	TObjectPtr<UQuestlineGraph> QuestlineGraph;
	TSharedPtr<SQuestlineGraphPanel> GraphEditorWidget;
	TSharedPtr<FUICommandList> GraphEditorCommands;
	static const FName GraphViewportTabId;
	
	void OnNodeTitleCommitted(const FText& NewText, ETextCommit::Type CommitType, UEdGraphNode* NodeBeingChanged);

	/*-----------------------------------------------------------------------------------
	 * Details Panel
	 *----------------------------------------------------------------------------------*/
	
	TSharedRef<SDockTab> SpawnDetailsTab(const FSpawnTabArgs& Args);
	void OnGraphSelectionChanged(const FGraphPanelSelectionSet& SelectedNodes);

	TSharedPtr<IDetailsView> DetailsView;
	static const FName DetailsTabId;

	/*-----------------------------------------------------------------------------------
	 * Questline Outliner Panel
	 *----------------------------------------------------------------------------------*/

	TSharedRef<SDockTab> SpawnOutlinerTab(const FSpawnTabArgs& Args);
	void OnOutlinerItemNavigate(TSharedPtr<FQuestlineOutlinerItem> Item);
	FEdNodeLocation FindEdNodeLocation(const FGuid& ContentGuid) const;

	TSharedPtr<SQuestlineOutlinerPanel> OutlinerPanel;
	static const FName OutlinerTabId;

	/*-----------------------------------------------------------------------------------
	 * Nested Graph Navigation
	 *----------------------------------------------------------------------------------*/

	void NavigateBack();
	void NavigateForward();
	TArray<FQuestlineBreadcrumb> BuildBreadcrumbs(UEdGraph* Graph) const;
	void OnNodeDoubleClicked(UEdGraphNode* Node);

	bool CanNavigateBack() const { return GraphBackwardStack.Num() > 1 || CrossAssetBackEditor.IsValid(); }
	bool CanNavigateForward() const { return GraphForwardStack.Num() > 0; }
	
	TArray<UEdGraph*> GraphBackwardStack;						// backwards-looking stack of opened graphs
	TArray<UEdGraph*> GraphForwardStack;						// populates when using 'back' button, cleared on NavigateTo
	TArray<FDelegateHandle> GraphChangedHandles;				// one per graph in stack
	TSharedPtr<SBox> GraphPanelContainer;						// swapped by NavigateTo
	TSharedPtr<SQuestlineBreadcrumbBar> BreadcrumbBar;			// updated by NavigateTo
	TWeakPtr<FQuestlineGraphEditor> CrossAssetBackEditor;		// manages navigation to and from linked questline assets 
	bool bIsNavigatingHistory = false;

public:
	void NavigateTo(UEdGraph* Graph);

	/** Navigate to and select the editor node matching ContentGuid within this editor's asset. */
	void NavigateToContentNode(const FGuid& ContentGuid);

	/** Navigate to this editor's root graph and select its Entry node. */
	void NavigateToEntry();
	
	void NavigateToLocation(UEdGraph* HostGraph, UEdGraphNode* EdNode);
	
};
