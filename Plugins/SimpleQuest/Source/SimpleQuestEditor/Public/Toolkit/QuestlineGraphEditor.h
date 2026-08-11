// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "EditorUndoClient.h"
#include "GameplayTagContainer.h"
#include "IDetailsView.h"
#include "Resolver/QuestPlanBroker.h"
#include "Toolkit/QuestlineBreadcrumbBar.h"


struct FQuestlineOutlinerItem;
class SGraphEditor;
class SGroupExaminerPanel;
class SPrereqExaminerPanel;
class SQuestPlanPanel;
class SQuestlineGraphPanel;
class SQuestlineOutlinerPanel;
class UQuestlineGraph;


class FQuestlineGraphEditor : public FAssetEditorToolkit, public FEditorUndoClient
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

	// FEditorUndoClient interface — GEditor calls these after any undo/redo affecting anything we've registered to watch.
	// Used here to force a full panel refresh; UE's per-UObject PostEditUndo propagation doesn't reliably invalidate
	// SGraphPanel's widget cache for all node types (UEdGraphNode_Comment in particular hangs on).
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override;
	
	struct FEdNodeLocation
	{
		UEdGraph* HostGraph = nullptr;
		UEdGraphNode* EdNode = nullptr;
		bool IsValid() const { return HostGraph && EdNode; }
	};

	/**
	 * Sets hover-highlight borders on the given nodes in this editor's active graph panel. Called by the Group Examiner's
	 * row hover handlers — cross-editor because any examiner can resolve any open editor via GetEditorForNode and dispatch
	 * here. Pass an empty array or use ClearNodeHighlight() to clear. Silent no-op if the panel isn't constructed yet.
	 */
	void HighlightNodesInViewport(const TArray<UEdGraphNode*>& Nodes);

	/** Clears the active graph panel's hover-highlight set. */
	void ClearNodeHighlight();
	
private:
	TSharedRef<SDockTab> SpawnGraphViewportTab(const FSpawnTabArgs& Args);
	SGraphEditor::FGraphEditorEvents MakeGraphEvents();
	TSharedRef<SQuestlineGraphPanel> CreateGraphEditorWidget();
	void BindGraphCommands();
	void DeleteSelectedNodes();
	bool CanDeleteNodes() const;

	// Standard edit commands — SGraphEditor doesn't wire these by default; each editor has to bind and implement.
	void CopySelectedNodes();
	bool CanCopyNodes() const;
	void CutSelectedNodes();
	bool CanCutNodes() const;
	void PasteNodes();
	void PasteNodesHere(UEdGraph* DestinationGraph, const FVector2f& GraphLocation);
	bool CanPasteNodes() const;
	void DuplicateNodes();
	bool CanDuplicateNodes() const;

	void OnCreateComment();
	
	// Compile and save graph data layer
	void CompileQuestlineGraph();
	void ValidatePrereqTags();
	virtual void SaveAsset_Execute() override;
	void ExtendToolbar();
	void FillToolbar(FToolBarBuilder& ToolbarBuilder);

	/**
	 * Toggles the Details panel between node-selection tracking and a pinned asset view — the Questline Graph's own
	 * properties (QuestlineID, DisplayName, etc.). Mirror of Blueprint's Class Defaults button. Click to pin; click
	 * again to restore normal selection tracking. When pinned, OnGraphSelectionChanged short-circuits to the asset
	 * regardless of node selection.
	 */
	void ToggleGraphDefaults();
	bool IsGraphDefaultsPinned() const { return bGraphDefaultsPinned; }

	/** Session-scoped; not serialized. Drives the toolbar toggle's checked state + OnGraphSelectionChanged's bypass. */
	bool bGraphDefaultsPinned = false;
	
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
	 * Prerequisite Examiner Panel
	 *----------------------------------------------------------------------------------*/
public:	
	/** Pins the Prerequisite Expression Examiner panel to the expression implied by ContextNode. Context node type
		determines what's shown — content nodes surface their Prerequisites input expression, combinators surface what
		feeds their output, Rule Entry/Exit surface the rule's Enter expression with rule-info header populated. */
	void PinPrereqExaminer(UEdGraphNode* ContextNode);
	
private:
	TSharedRef<SDockTab> SpawnPrereqExaminerTab(const FSpawnTabArgs& Args);
	TSharedPtr<SPrereqExaminerPanel> PrereqExaminerPanel;
	static const FName PrereqExaminerTabId;
	
	/*-----------------------------------------------------------------------------------
	 * Group Examiner Panel
	 *----------------------------------------------------------------------------------*/
public:
	/**
	 * Opens the Group Examiner tab (if closed), pins the given group tag, and triggers a topology rebuild. Invoked by the
	 * "Examine Group Connections" context-menu action on activation group nodes.
	 */
	void PinGroupExaminer(FGameplayTag GroupTag, UEdGraphNode* PinnedEndpointNode, UEdGraphNode* RowToHighlight = nullptr) const;
	
private:
	TSharedRef<SDockTab> SpawnGroupExaminerTab(const FSpawnTabArgs& Args);
	
	TSharedPtr<SGroupExaminerPanel> GroupExaminerPanel;
	static const FName GroupExaminerTabId;
	
	TSharedRef<SDockTab> SpawnPlanTab(const FSpawnTabArgs& Args);
	TSharedPtr<SQuestPlanPanel> PlanPanel;
	static const FName PlanTabId;

	void OpenSourceData();

	/** SELECTION - what the next Build Plan will read. A different fact from the displayed plan's provenance. */
	FString GetImportFolder() const { return LastImportSource.Folder; }
	void HandleImportFolderChanged(const FString& NewFolder);
	FSoftObjectPath GetImportTable() const { return LastImportSource.Table; }
	void HandleImportTableChanged(const FSoftObjectPath& NewTable);
	void HandleSourceKindChanged(EQuestPlanSourceKind NewKind);
	FString GetImportFormatName() const { return LastImportSource.FormatName; }
	void HandleImportFormatChanged(FString NewFormat);
	FSoftObjectPath GetImportMappingPath() const { return LastImportSource.Mapping; }
	void HandleImportMappingChanged(const FSoftObjectPath& NewMapping);

	void BrowseForImportFolder();
	void BuildImportPlan();
	bool CanBuildImportPlan() const;
	void ApplyImportPlan();
	bool CanApplyImportPlan() const;
	
	/**
	 * Writes this questline out through the endpoint the panel is showing. Shares the SELECTION with the import
	 * direction - one questline has one text form, and two independent format pickers is a machine for producing the
	 * conversion refusal by accident.
	 */
	void ExportQuestlineData();
	bool CanExportQuestlineData() const;

	/** PROVENANCE - what the displayed plan was built from. EMPTY when there is no plan, so the slot collapses. */
	FText GetPlanProvenanceLabel() const;

	/** True when the selection has moved away from what the displayed plan was built from. */
	bool IsPlanSourceStale() const;

	/**
	 * Runs against the source it is GIVEN rather than the held selection, because the two callers legitimately want
	 * different ones: Build Plan reads the current selection, Apply re-runs what the reviewed plan came from and must
	 * not overwrite a field the designer has edited since. Assign-then-run is how a table provenance got dropped once.
	 */
	bool RunImport(const FQuestPlanSource& Source, bool bApply);
	
	/**
	 * Per-user, per-project memory of this questline's source, so a session resumes where the last one ended. Restored
	 * once at init; saved from every site that moves LastImportSource or LastImportMapping, because a choice that
	 * survives only until the editor closes is worse than none - it looks remembered right up until it isn't.
	 */
	void RestoreImportEndpointFromMemo();
	void SaveImportEndpointToMemo() const;

	/**
	 * What the next read will use, and what Apply and Rebuild re-run. Held as the BROKER's source type rather than a
	 * loose folder+format pair so a DataTable provenance survives the round trip through a plan record: the loose pair
	 * could only ever describe a folder, so a table plan published by the console arrived here as no source at all.
	 * FormatName is seeded in InitQuestlineGraphEditor rather than by a braced initializer here - the first two members
	 * are both FStrings, so a positional default would mis-assign silently if they were ever reordered.
	 */
	FQuestPlanSource LastImportSource;
	
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

	bool bSuppressDirtyOnGraphChange = false;					// set true across RefreshAllNodeWidgets — a compile-triggered
																// refresh fires NotifyGraphChanged on every descendant graph, but
																// it's NOT a user edit; without this guard the status icon bounces
																// to Unknown when neighbor-asset broadcasts pass through our refresh.

public:
	void NavigateTo(UEdGraph* Graph);

	/** Navigate to and select the editor node matching ContentGuid within this editor's asset. */
	void NavigateToContentNode(const FGuid& ContentGuid);

	/** Navigate to this editor's root graph and select its Entry node. */
	void NavigateToEntry();
	
	void NavigateToLocation(UEdGraph* HostGraph, UEdGraphNode* EdNode);
	
};
