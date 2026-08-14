// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Toolkit/QuestlineGraphEditor.h"

#include "DesktopPlatformModule.h"
#include "EdGraphUtilities.h"
#include "Toolkit/QuestlineGraphPanel.h"
#include "Quests/QuestlineGraph.h"
#include "GraphEditor.h"
#include "GraphEditorActions.h"
#include "ISimpleQuestEditorModule.h"
#include "Utilities/QuestlineGraphCompiler.h"
#include "Widgets/Docking/SDockTab.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/Commands/GenericCommands.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Toolkit/QuestlineGraphEditorCommands.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "PropertyEditorModule.h"
#include "SimpleQuestLog.h"
#include "SNodePanel.h"
#include "Modules/ModuleManager.h"
#include "Nodes/QuestlineNode_Entry.h"
#include "Nodes/QuestlineNode_LinkedQuestline.h"
#include "Nodes/QuestlineNode_Quest.h"
#include "Quests/QuestNodeBase.h"
#include "Toolkit/QuestlineOutlinerPanel.h"
#include "Utilities/SimpleQuestEditorUtils.h"
#include "Widgets/SGroupExaminerPanel.h"
#include "Widgets/SPrereqExaminerPanel.h"
#include "HAL/PlatformApplicationMisc.h"
#include "EdGraphNode_Comment.h"
#include "GameplayTagsManager.h"
#include "GraphEditorActions.h"
#include "IDesktopPlatform.h"
#include "ScopedTransaction.h"
#include "Resolver/QuestExportOperations.h"
#include "Resolver/QuestImportOperations.h"
#include "Resolver/QuestPlanBroker.h"
#include "Resolver/QuestPlanReport.h"
#include "Resolver/QuestResolverEditorMemo.h"
#include "Resolver/QuestRowApply.h"
#include "Resolver/SQuestPlanPanel.h"


const FName FQuestlineGraphEditor::GraphViewportTabId(TEXT("QuestlineGraphEditor_GraphViewport"));
const FName FQuestlineGraphEditor::DetailsTabId(TEXT("QuestlineGraphEditor_Details"));
const FName FQuestlineGraphEditor::OutlinerTabId(TEXT("QuestlineGraphEditor_Outliner"));
const FName FQuestlineGraphEditor::GroupExaminerTabId(TEXT("QuestlineGraphEditor_GroupExaminer"));
const FName FQuestlineGraphEditor::PrereqExaminerTabId(TEXT("QuestlineGraphEditor_PrereqExaminer"));
const FName FQuestlineGraphEditor::PlanTabId(TEXT("QuestlineGraphEditor_Plan"));


FQuestlineGraphEditor::~FQuestlineGraphEditor()
{
    ISimpleQuestEditorModule::Get().OnQuestlineCompiled().Remove(ExternalCompileHandle);

    if (GEditor) GEditor->UnregisterForUndo(this);
    
    for (int32 i = 0; i < GraphBackwardStack.Num(); ++i)
    {
        if (GraphBackwardStack[i])
        {
            GraphBackwardStack[i]->RemoveOnGraphChangedHandler(GraphChangedHandles[i]);
        }
    }

    /**
     * Defensive hover-highlight cleanup — if this editor's Group Examiner set highlights on other editors' viewports and
     * got destroyed before a proper OnMouseLeave could fire (rare, but possible on abrupt editor close), stale borders
     * would linger. Iterate all currently-open questline editors and clear. Skip self (already being destroyed).
     */
    if (GEditor)
    {
        if (UAssetEditorSubsystem* EditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
        {
            TArray<UObject*> EditedAssets = EditorSubsystem->GetAllEditedAssets();
            for (UObject* Asset : EditedAssets)
            {
                if (PrereqExaminerPanel.IsValid())
                {
                    PrereqExaminerPanel->PinContextNode(nullptr);
                }
                
                UQuestlineGraph* AssetGraph = Cast<UQuestlineGraph>(Asset);
                if (!AssetGraph) continue;

                IAssetEditorInstance* Instance = EditorSubsystem->FindEditorForAsset(AssetGraph, false);
                if (!Instance) continue;

                FQuestlineGraphEditor* OtherEditor = static_cast<FQuestlineGraphEditor*>(Instance);
                if (OtherEditor == this) continue;

                OtherEditor->ClearNodeHighlight();
            }
        }
    }
}

void FQuestlineGraphEditor::InitQuestlineGraphEditor(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UQuestlineGraph* InQuestlineGraph)
{
    CrossAssetBackEditor.Reset();
    QuestlineGraph = InQuestlineGraph;
    // Seeded before the restore so a questline with no memory still gets a usable format; TSV matches what the console
    // defaults to, so both callers start in the same reading. Not a member initializer - see the declaration for why a
    // braced default is a hazard on this struct.
    LastImportSource.FormatName = TEXT("TSV");
    RestoreImportEndpointFromMemo();
    ExternalCompileHandle = ISimpleQuestEditorModule::Get().OnQuestlineCompiled().AddSP(this, &FQuestlineGraphEditor::OnExternalCompile);
    
    const TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout("QuestlineGraphEditor_Layout_v11")
        ->AddArea
        (
            FTabManager::NewPrimaryArea()
            ->SetOrientation(Orient_Horizontal)
            ->Split
            (
                FTabManager::NewSplitter()
                ->SetOrientation(Orient_Vertical)
                ->SetSizeCoefficient(0.2f)
                ->Split
                (
                    FTabManager::NewStack()
                    ->SetSizeCoefficient(0.4f)
                    ->AddTab(OutlinerTabId, ETabState::OpenedTab)
                )
                ->Split
                (
                    FTabManager::NewStack()
                    ->SetSizeCoefficient(0.3f)
                    ->AddTab(GroupExaminerTabId, ETabState::OpenedTab)
                    ->AddTab(PrereqExaminerTabId, ETabState::OpenedTab)
                )
                ->Split
                (
                    FTabManager::NewStack()
                    ->SetSizeCoefficient(0.3f)
                    ->AddTab(DetailsTabId, ETabState::OpenedTab)
                )
            )
            ->Split
            (
                // The plan sits UNDER the graph rather than in the left column. Its data is landscape - three columns
                // where one carries before-and-after values - and it is a read-then-act-then-dismiss surface, which is
                // where every UE editor puts the Message Log. The left column holds things you keep an eye on, and
                // stacking there would evict the Details panel you want open WHILE reviewing a plan.
                FTabManager::NewSplitter()
                ->SetOrientation(Orient_Vertical)
                ->SetSizeCoefficient(0.80f)
                ->Split
                (
                    FTabManager::NewStack()
                    ->SetSizeCoefficient(0.72f)
                    ->AddTab(GraphViewportTabId, ETabState::OpenedTab)
                )
                ->Split
                (
                    FTabManager::NewStack()
                    ->SetSizeCoefficient(0.28f)
                    ->AddTab(PlanTabId, ETabState::ClosedTab)
                )
            )
        );
    
    BindGraphCommands();
    ExtendToolbar();

    FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
    FDetailsViewArgs DetailsViewArgs;
    DetailsViewArgs.bHideSelectionTip = true;
    DetailsViewArgs.bAllowSearch = false;
    DetailsView = PropertyModule.CreateDetailView(DetailsViewArgs);

    // Initial target — show the asset itself (QuestlineID, DisplayName, etc.) until the designer selects a node.
    // Matches UE asset-editor convention where empty selection surfaces the asset's properties.
    if (DetailsView.IsValid() && QuestlineGraph)
    {
        DetailsView->SetObject(QuestlineGraph);
    }

    InitAssetEditor(
        Mode,
        InitToolkitHost,
        FName(TEXT("QuestlineGraphEditorApp")),
        Layout,
        true,  // bCreateDefaultStandaloneMenu
        true,  // bCreateDefaultToolbar
        InQuestlineGraph);

    if (GEditor) GEditor->RegisterForUndo(this);
}

FName FQuestlineGraphEditor::GetToolkitFName() const
{
    return FName(TEXT("QuestlineGraphEditor"));
}

FText FQuestlineGraphEditor::GetBaseToolkitName() const
{
    return NSLOCTEXT("SimpleQuestEditor", "QuestlineGraphEditorToolkit", "Questline Graph Editor");
}

FString FQuestlineGraphEditor::GetWorldCentricTabPrefix() const
{
    return TEXT("QuestlineGraph ");
}

FLinearColor FQuestlineGraphEditor::GetWorldCentricTabColorScale() const
{
    return FLinearColor(0.18f, 0.67f, 0.51f, 0.5f);
}

void FQuestlineGraphEditor::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
    FAssetEditorToolkit::RegisterTabSpawners(InTabManager);
    
    WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(
            NSLOCTEXT("SimpleQuestEditor", "WorkspaceMenu_QuestlineGraphEditor", "Questline Graph Editor"));

    InTabManager->RegisterTabSpawner(
        GraphViewportTabId,
        FOnSpawnTab::CreateSP(this, &FQuestlineGraphEditor::SpawnGraphViewportTab))
        .SetDisplayName(NSLOCTEXT("SimpleQuestEditor", "GraphViewportTab", "Questline Graph Editor"))
        .SetIcon(FSlateIcon("SimpleQuestStyle", "ClassIcon.QuestlineGraph"))
        .SetGroup(WorkspaceMenuCategory.ToSharedRef());

    InTabManager->RegisterTabSpawner(
        DetailsTabId,
        FOnSpawnTab::CreateSP(this, &FQuestlineGraphEditor::SpawnDetailsTab))
        .SetDisplayName(NSLOCTEXT("SimpleQuestEditor", "DetailsTab", "Details"))
        .SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Details"))
        .SetGroup(WorkspaceMenuCategory.ToSharedRef());

    InTabManager->RegisterTabSpawner(
        OutlinerTabId,
        FOnSpawnTab::CreateSP(this, &FQuestlineGraphEditor::SpawnOutlinerTab))
        .SetDisplayName(NSLOCTEXT("SimpleQuestEditor", "OutlinerTab", "Questline Outliner"))
        .SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Outliner"))
        .SetGroup(WorkspaceMenuCategory.ToSharedRef());

    InTabManager->RegisterTabSpawner(
        GroupExaminerTabId,
        FOnSpawnTab::CreateSP(this, &FQuestlineGraphEditor::SpawnGroupExaminerTab))
        .SetDisplayName(NSLOCTEXT("SimpleQuestEditor", "GroupExaminerTab", "Group Examiner"))
        .SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "ContentBrowser.ReferenceViewer"))
        .SetGroup(WorkspaceMenuCategory.ToSharedRef());

    InTabManager->RegisterTabSpawner(PrereqExaminerTabId,
        FOnSpawnTab::CreateSP(this, &FQuestlineGraphEditor::SpawnPrereqExaminerTab))
        .SetDisplayName(NSLOCTEXT("SimpleQuestEditor", "PrereqExaminerTabLabel", "Prereq Examiner"))
        .SetGroup(WorkspaceMenuCategory.ToSharedRef())
        .SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "BlueprintEditor.FindInBlueprint"));
    InTabManager->RegisterTabSpawner(PlanTabId,
        FOnSpawnTab::CreateSP(this, &FQuestlineGraphEditor::SpawnPlanTab))
        .SetDisplayName(NSLOCTEXT("SimpleQuestEditor", "SourceDataTabLabel", "Source Data"))
        .SetGroup(WorkspaceMenuCategory.ToSharedRef())
        .SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "ContentBrowser.AssetActions.Duplicate"));
}

void FQuestlineGraphEditor::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
    InTabManager->UnregisterTabSpawner(GraphViewportTabId);
    InTabManager->UnregisterTabSpawner(DetailsTabId);
    InTabManager->UnregisterTabSpawner(OutlinerTabId);
    InTabManager->UnregisterTabSpawner(GroupExaminerTabId);
    InTabManager->UnregisterTabSpawner(PrereqExaminerTabId);
    InTabManager->UnregisterTabSpawner(PlanTabId);
    FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);
}

void FQuestlineGraphEditor::PostUndo(bool bSuccess)
{
    // Force the current graph panel to rebuild its node widgets after any undo. UE's per-UObject PostEditUndo
    // mechanism is fine for nodes that inherit from UQuestlineNodeBase (they explicitly broadcast NotifyGraphChanged),
    // but third-party node classes like UEdGraphNode_Comment don't, so their widgets can linger after the underlying
    // Nodes-array entry has been rolled back. A blanket NotifyGraphChanged from here covers every case uniformly.
    if (GraphEditorWidget.IsValid())
    {
        if (TSharedPtr<SGraphEditor> Inner = GraphEditorWidget->GetGraphEditor())
        {
            if (UEdGraph* Graph = Inner->GetCurrentGraph())
            {
                Graph->NotifyGraphChanged();
            }
        }
    }
}

void FQuestlineGraphEditor::PostRedo(bool bSuccess)
{
    // Redo runs through the same refresh path as undo — identical invalidation concern in both directions.
    PostUndo(bSuccess);
}

TSharedRef<SDockTab> FQuestlineGraphEditor::SpawnGraphViewportTab(const FSpawnTabArgs& Args)
{
    GraphPanelContainer = SNew(SBox);

    SAssignNew(BreadcrumbBar, SQuestlineBreadcrumbBar)
        .OnCrumbClicked(FOnQuestlineCrumbClicked::CreateSP(this, &FQuestlineGraphEditor::NavigateTo))
        .OnDelimiterClicked(FOnQuestlineDelimiterClicked::CreateSP(this, &FQuestlineGraphEditor::NavigateToLocation));

    NavigateTo(QuestlineGraph->QuestlineEdGraph);

    return SNew(SDockTab)
        .Label(NSLOCTEXT("SimpleQuestEditor", "GraphViewportTabLabel", "Questline Graph"))
        [
            SNew(SOverlay)

            + SOverlay::Slot()
            .VAlign(VAlign_Fill)
            .HAlign(HAlign_Fill)
            [
                GraphPanelContainer.ToSharedRef()
            ]

            + SOverlay::Slot()
            .VAlign(VAlign_Top)
            .HAlign(HAlign_Fill)
            [
                SNew(SBorder)
                .BorderImage(FAppStyle::GetBrush("Graph.TitleBackground"))
                .HAlign(HAlign_Fill)
                [
                    SNew(SHorizontalBox)

                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    .VAlign(VAlign_Center)
                    [
                        SNew(SButton)
                        .ButtonStyle(FAppStyle::Get(), "GraphBreadcrumbButton")
                        .OnClicked_Lambda([this]() { NavigateBack(); return FReply::Handled(); })
                        .IsEnabled(this, &FQuestlineGraphEditor::CanNavigateBack)
                        .ToolTip(FQuestlineGraphEditorCommands::Get().NavigateBack->MakeTooltip())
                        [
                            SNew(SImage)
                            .Image(FAppStyle::GetBrush("GraphBreadcrumb.BrowseBack"))
                        ]
                    ]

                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    .VAlign(VAlign_Center)
                    [
                        SNew(SButton)
                        .ButtonStyle(FAppStyle::Get(), "GraphBreadcrumbButton")
                        .OnClicked_Lambda([this]() { NavigateForward(); return FReply::Handled(); })
                        .IsEnabled(this, &FQuestlineGraphEditor::CanNavigateForward)
                        .ToolTip(FQuestlineGraphEditorCommands::Get().NavigateForward->MakeTooltip())
                        [
                            SNew(SImage)
                            .Image(FAppStyle::GetBrush("GraphBreadcrumb.BrowseForward"))
                        ]
                    ]

                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    .VAlign(VAlign_Fill)
                    [
                        SNew(SSeparator)
                        .Orientation(Orient_Vertical)
                    ]

                    + SHorizontalBox::Slot()
                    .FillWidth(1.f)
                    .VAlign(VAlign_Center)
                    .Padding(4.f, 0.f)
                    [
                        BreadcrumbBar.ToSharedRef()
                    ]
                ]
            ]
        ];
}

SGraphEditor::FGraphEditorEvents FQuestlineGraphEditor::MakeGraphEvents()
{
    SGraphEditor::FGraphEditorEvents GraphEvents;
    GraphEvents.OnSelectionChanged = SGraphEditor::FOnSelectionChanged::CreateSP(this, &FQuestlineGraphEditor::OnGraphSelectionChanged);
    GraphEvents.OnNodeDoubleClicked = FSingleNodeEvent::CreateSP(this, &FQuestlineGraphEditor::OnNodeDoubleClicked);
    GraphEvents.OnTextCommitted = FOnNodeTextCommitted::CreateSP(this, &FQuestlineGraphEditor::OnNodeTitleCommitted);
    return GraphEvents;
}

TSharedRef<SQuestlineGraphPanel> FQuestlineGraphEditor::CreateGraphEditorWidget()
{
    check(QuestlineGraph);
    check(QuestlineGraph->QuestlineEdGraph);

    return SNew(SQuestlineGraphPanel, QuestlineGraph->QuestlineEdGraph, GraphEditorCommands)
        .GraphEvents(MakeGraphEvents());
}

void FQuestlineGraphEditor::BindGraphCommands()
{
    FGraphEditorCommands::Register();

    GraphEditorCommands = MakeShared<FUICommandList>();
    
    GraphEditorCommands->MapAction(
        FGenericCommands::Get().Delete,
        FExecuteAction::CreateSP(this, &FQuestlineGraphEditor::DeleteSelectedNodes),
        FCanExecuteAction::CreateSP(this, &FQuestlineGraphEditor::CanDeleteNodes));

    GraphEditorCommands->MapAction(
        FGenericCommands::Get().Copy,
        FExecuteAction::CreateSP(this, &FQuestlineGraphEditor::CopySelectedNodes),
        FCanExecuteAction::CreateSP(this, &FQuestlineGraphEditor::CanCopyNodes));

    GraphEditorCommands->MapAction(
        FGenericCommands::Get().Cut,
        FExecuteAction::CreateSP(this, &FQuestlineGraphEditor::CutSelectedNodes),
        FCanExecuteAction::CreateSP(this, &FQuestlineGraphEditor::CanCutNodes));

    GraphEditorCommands->MapAction(
        FGenericCommands::Get().Paste,
        FExecuteAction::CreateSP(this, &FQuestlineGraphEditor::PasteNodes),
        FCanExecuteAction::CreateSP(this, &FQuestlineGraphEditor::CanPasteNodes));

    GraphEditorCommands->MapAction(
        FGenericCommands::Get().Duplicate,
        FExecuteAction::CreateSP(this, &FQuestlineGraphEditor::DuplicateNodes),
        FCanExecuteAction::CreateSP(this, &FQuestlineGraphEditor::CanDuplicateNodes));

    GraphEditorCommands->MapAction(
        FGraphEditorCommands::Get().CreateComment,
        FExecuteAction::CreateSP(this, &FQuestlineGraphEditor::OnCreateComment));

    GraphEditorCommands->MapAction(
       FQuestlineGraphEditorCommands::Get().CompileQuestlineGraph,
       FExecuteAction::CreateSP(this, &FQuestlineGraphEditor::CompileQuestlineGraph));

    GraphEditorCommands->MapAction(
        FQuestlineGraphEditorCommands::Get().OpenSourceData,
        FExecuteAction::CreateSP(this, &FQuestlineGraphEditor::OpenSourceData));

    GraphEditorCommands->MapAction(
        FQuestlineGraphEditorCommands::Get().ApplyImportPlan,
        FExecuteAction::CreateSP(this, &FQuestlineGraphEditor::ApplyImportPlan),
        FCanExecuteAction::CreateSP(this, &FQuestlineGraphEditor::CanApplyImportPlan));

    GraphEditorCommands->MapAction(
        FQuestlineGraphEditorCommands::Get().CompileAllQuestlineGraphs,
        FExecuteAction::CreateLambda([]()
        {
            ISimpleQuestEditorModule::Get().CompileAllQuestlineGraphs();
        }));
    
    GraphEditorCommands->MapAction(
        FQuestlineGraphEditorCommands::Get().ValidatePrereqTags,
        FExecuteAction::CreateSP(this, &FQuestlineGraphEditor::ValidatePrereqTags));
    
    GraphEditorCommands->MapAction(
        FQuestlineGraphEditorCommands::Get().ToggleGraphDefaults,
        FExecuteAction::CreateSP(this, &FQuestlineGraphEditor::ToggleGraphDefaults),
        FCanExecuteAction(),
        FIsActionChecked::CreateSP(this, &FQuestlineGraphEditor::IsGraphDefaultsPinned));
    
    GetToolkitCommands()->MapAction(
        FQuestlineGraphEditorCommands::Get().NavigateBack,
        FExecuteAction::CreateSP(this, &FQuestlineGraphEditor::NavigateBack),
        FCanExecuteAction::CreateSP(this, &FQuestlineGraphEditor::CanNavigateBack));

    GetToolkitCommands()->MapAction(
        FQuestlineGraphEditorCommands::Get().NavigateForward,
        FExecuteAction::CreateSP(this, &FQuestlineGraphEditor::NavigateForward),
        FCanExecuteAction::CreateSP(this, &FQuestlineGraphEditor::CanNavigateForward));
}

void FQuestlineGraphEditor::DeleteSelectedNodes()
{
    if (!GraphEditorWidget.IsValid()) return;

    UEdGraph* CurrentGraph = GraphBackwardStack.IsEmpty() ? nullptr : GraphBackwardStack.Last();
    if (!CurrentGraph) return;

    const FScopedTransaction Transaction(NSLOCTEXT("SimpleQuestEditor", "DeleteSelectedNodes", "Delete Selected Nodes"));

    CurrentGraph->Modify();

    for (UObject* Obj : GraphEditorWidget->GetGraphEditor()->GetSelectedNodes())
    {
        UEdGraphNode* Node = Cast<UEdGraphNode>(Obj);
        if (Node && Node->CanUserDeleteNode())
        {
            CurrentGraph->RemoveNode(Node);
        }
    }
}

bool FQuestlineGraphEditor::CanDeleteNodes() const
{
    if (!GraphEditorWidget.IsValid()) return false;
    for (UObject* Obj : GraphEditorWidget->GetGraphEditor()->GetSelectedNodes())
    {
        if (const UEdGraphNode* Node = Cast<UEdGraphNode>(Obj))
        {
            if (Node->CanUserDeleteNode()) return true;
        }
    }
    return false;
}

void FQuestlineGraphEditor::CopySelectedNodes()
{
    if (!GraphEditorWidget.IsValid()) return;
    const FGraphPanelSelectionSet SelectedNodes = GraphEditorWidget->GetGraphEditor()->GetSelectedNodes();

    for (UObject* Obj : SelectedNodes)
    {
        if (UEdGraphNode* Node = Cast<UEdGraphNode>(Obj))
        {
            Node->PrepareForCopying();
        }
    }

    FString ExportedText;
    FEdGraphUtilities::ExportNodesToText(SelectedNodes, ExportedText);
    FPlatformApplicationMisc::ClipboardCopy(*ExportedText);
}

bool FQuestlineGraphEditor::CanCopyNodes() const
{
    if (!GraphEditorWidget.IsValid()) return false;
    for (UObject* Obj : GraphEditorWidget->GetGraphEditor()->GetSelectedNodes())
    {
        if (const UEdGraphNode* Node = Cast<UEdGraphNode>(Obj))
        {
            if (Node->CanDuplicateNode()) return true;
        }
    }
    return false;
}

void FQuestlineGraphEditor::CutSelectedNodes()
{
    CopySelectedNodes();
    // Existing DeleteSelectedNodes already gates per-node on CanUserDeleteNode, so nothing uncuttable gets removed.
    DeleteSelectedNodes();
}

bool FQuestlineGraphEditor::CanCutNodes() const
{
    return CanCopyNodes() && CanDeleteNodes();
}

void FQuestlineGraphEditor::PasteNodes()
{
    if (!GraphEditorWidget.IsValid()) return;
    const TSharedPtr<SGraphEditor> GraphEd = GraphEditorWidget->GetGraphEditor();
    if (!GraphEd.IsValid()) return;
    PasteNodesHere(GraphEd->GetCurrentGraph(), GraphEd->GetPasteLocation2f());
}

void FQuestlineGraphEditor::PasteNodesHere(UEdGraph* DestinationGraph, const FVector2f& GraphLocation)
{
    if (!DestinationGraph || !GraphEditorWidget.IsValid()) return;
    const TSharedPtr<SGraphEditor> GraphEd = GraphEditorWidget->GetGraphEditor();
    if (!GraphEd.IsValid()) return;

    const FScopedTransaction Transaction(FGenericCommands::Get().Paste->GetDescription());
    DestinationGraph->Modify();

    // Newly-pasted nodes become the selection.
    GraphEd->ClearSelectionSet();

    FString TextToImport;
    FPlatformApplicationMisc::ClipboardPaste(TextToImport);

    TSet<UEdGraphNode*> PastedNodes;
    FEdGraphUtilities::ImportNodesFromText(DestinationGraph, TextToImport, PastedNodes);
    if (PastedNodes.Num() == 0) return;

    // Average original position so we can recentre at GraphLocation while preserving relative offsets.
    FVector2f AvgNodePosition(0.f, 0.f);
    for (UEdGraphNode* Node : PastedNodes)
    {
        AvgNodePosition.X += Node->NodePosX;
        AvgNodePosition.Y += Node->NodePosY;
    }
    const float InvNumNodes = 1.0f / PastedNodes.Num();
    AvgNodePosition.X *= InvNumNodes;
    AvgNodePosition.Y *= InvNumNodes;

    for (UEdGraphNode* Node : PastedNodes)
    {
        GraphEd->SetNodeSelection(Node, true);

        Node->NodePosX = static_cast<int32>((Node->NodePosX - AvgNodePosition.X) + GraphLocation.X);
        Node->NodePosY = static_cast<int32>((Node->NodePosY - AvgNodePosition.Y) + GraphLocation.Y);
        Node->SnapToGrid(SNodePanel::GetSnapGridSize());

        // UEdGraphNode::NodeGuid is separate from our QuestGuid (handled inside PostPasteNode). Both need refresh.
        Node->CreateNewGuid();
    }

    DestinationGraph->NotifyGraphChanged();
}

bool FQuestlineGraphEditor::CanPasteNodes() const
{
    if (!GraphEditorWidget.IsValid()) return false;
    const TSharedPtr<SGraphEditor> GraphEd = GraphEditorWidget->GetGraphEditor();
    if (!GraphEd.IsValid()) return false;
    FString ClipboardContent;
    FPlatformApplicationMisc::ClipboardPaste(ClipboardContent);
    return FEdGraphUtilities::CanImportNodesFromText(GraphEd->GetCurrentGraph(), ClipboardContent);
}

void FQuestlineGraphEditor::DuplicateNodes()
{
    CopySelectedNodes();
    PasteNodes();
}

bool FQuestlineGraphEditor::CanDuplicateNodes() const
{
    return CanCopyNodes();
}

void FQuestlineGraphEditor::OnCreateComment()
{
    if (!GraphEditorWidget.IsValid()) return;
    TSharedPtr<SGraphEditor> Inner = GraphEditorWidget->GetGraphEditor();
    if (!Inner.IsValid()) return;

    UEdGraph* Graph = Inner->GetCurrentGraph();
    if (!Graph) return;

    const FScopedTransaction Transaction(NSLOCTEXT("SimpleQuestEditor", "CreateCommentNode", "Create Comment"));
    Graph->Modify();

    // If nodes are selected, size the new comment to enclose their bounds with standard 50px padding.
    // Otherwise, spawn a small default-sized comment at the cursor / last paste location.
    FSlateRect SelBounds;
    const bool bHasSelection = Inner->GetBoundsForSelectedNodes(SelBounds, 50.0f);

    const FVector2f SpawnPos = bHasSelection
        ? FVector2f(SelBounds.Left, SelBounds.Top)
        : Inner->GetPasteLocation2f();

    UEdGraphNode_Comment* Comment = NewObject<UEdGraphNode_Comment>(Graph);
    Comment->SetFlags(RF_Transactional);
    Graph->AddNode(Comment, true, true);
    Comment->CreateNewGuid();
    Comment->PostPlacedNewNode();
    Comment->AllocateDefaultPins();

    Comment->NodePosX = FMath::RoundToInt(SpawnPos.X);
    Comment->NodePosY = FMath::RoundToInt(SpawnPos.Y);
    Comment->NodeWidth  = bHasSelection ? FMath::RoundToInt(SelBounds.Right - SelBounds.Left) : 400;
    Comment->NodeHeight = bHasSelection ? FMath::RoundToInt(SelBounds.Bottom - SelBounds.Top) : 100;

    Graph->NotifyGraphChanged();
}

void FQuestlineGraphEditor::CompileQuestlineGraph()
{
    // Aggregate state across primary + linked neighborhood.
    int32 TotalErrors = 0;
    int32 TotalWarnings = 0;
    int32 TotalRenamedActors = 0;
    int32 NeighborSuccessCount = 0;
    int32 NeighborFailCount = 0;
    TMap<FName, FName> AllRenames;

    FMessageLog CompilerLog("QuestCompiler");
    bool bLogPageOpen = false;
    auto EnsurePage = [&]()
    {
        if (bLogPageOpen) return;
        CompilerLog.NewPage(FText::Format(NSLOCTEXT("SimpleQuestEditor", "CompilePageLabel", "{0}"),
            FText::FromString(QuestlineGraph->GetName())));
        bLogPageOpen = true;
    };

    // Per-asset compile body — runs for primary and each neighbor. Broadcasts OnQuestlineCompiled per asset so every open
    // editor (this one + any others) refreshes via the existing OnExternalCompile path. Renames accumulate into AllRenames
    // for a single coalesced redirect write at end-of-batch.
    auto CompileAsset = [&](UQuestlineGraph* Graph, bool bIsPrimary)
    {
        if (!Graph) return;

        TUniquePtr<FQuestlineGraphCompiler> Compiler = ISimpleQuestEditorModule::Get().CreateCompiler();
        const bool bSuccess = Compiler->Compile(Graph);

        // Capture rename intent regardless of compile success. Renames are detected via the GUID bridge — a
        // structural property of the graph that's valid whether or not unrelated nodes failed validation in
        // the same compile. Gating behind bSuccess silently drops the rename when ANY error fires elsewhere
        // in the graph: RegisterCompiledTags still registers the new tag (so the picker updates), but the
        // OldName → NewName redirect never lands, and loaded actor instances keep their stale tags with
        // nothing in the redirect map to heal them.
        AllRenames.Append(Compiler->GetDetectedRenames());

        if (bSuccess)
        {
            ISimpleQuestEditorModule::Get().AccumulateCompiledDisplay(Graph);
            if (!bIsPrimary) ++NeighborSuccessCount;
        }
        else if (!bIsPrimary)
        {
            ++NeighborFailCount;
        }
        TotalErrors += Compiler->GetNumErrors();
        TotalWarnings += Compiler->GetNumWarnings();

        if (Compiler->GetMessages().Num() > 0)
        {
            EnsurePage();
            CompilerLog.AddMessages(Compiler->GetMessages());
        }

        ISimpleQuestEditorModule::Get().OnQuestlineCompiled().Broadcast(Graph->GetOutermost()->GetName(), bSuccess);
    };

    // Single batch covers primary + every linked neighbor + the coalesced WriteGameplayTagRedirects call. RegisterCompiledTags
    // inside Compile() takes the batched path (no per-graph WriteCompiledTagsIni / RebuildNativeTags), so the final rebuild fires
    // ONCE in EndCompileBatch — under the new redirect map written below — and registers each new canonical name as itself.
    {
        ISimpleQuestEditorModule::Get().BeginCompileBatch();
        ON_SCOPE_EXIT { ISimpleQuestEditorModule::Get().EndCompileBatch(); };

        // Primary first so its status/outliner update (via OnExternalCompile's bIsOwnAsset branch) reflects its own result.
        CompileAsset(QuestlineGraph, true);

        // Linked neighborhood — bidirectional transitive closure of LinkedQuestline references.
        TArray<UQuestlineGraph*> Neighborhood;
        ISimpleQuestEditorModule::Get().CollectLinkedNeighborhood(QuestlineGraph, Neighborhood);

        if (Neighborhood.Num() > 0)
        {
            UE_LOG(LogSimpleQuest, Log, TEXT("Compile: auto-compiling %d linked neighbor(s) of '%s'"), Neighborhood.Num(), *QuestlineGraph->GetName());
        }

        for (UQuestlineGraph* Neighbor : Neighborhood)
        {
            CompileAsset(Neighbor, false);
        }

        // Coalesced redirect write inside the batch scope so EndCompileBatch's RebuildNativeTags fires AFTER the CDO + manager
        // redirect-map are current.
        if (AllRenames.Num() > 0)
        {
            FSimpleQuestEditorUtilities::WriteGameplayTagRedirects(AllRenames);
        }
    }

    // Swap helpers run AFTER the batch closes — the tag tree reflects the new redirects and the new canonical names are
    // registered as themselves, so RequestGameplayTag inside the helpers returns valid tags rather than silently clearing
    // adopter data on cycle-closing renames.
    if (AllRenames.Num() > 0)
    {
        TotalRenamedActors = FSimpleQuestEditorUtilities::ApplyTagRenamesToLoadedWorlds(AllRenames);
        FSimpleQuestEditorUtilities::ApplyTagRenamesToLoadedBlueprintCDOs(AllRenames);
        FSimpleQuestEditorUtilities::ApplyTagRenamesToLoadedAssets(AllRenames);
    }

    // Notifications — MessageLog already shows pages if anything wrote to them. Emit a notify summary for
    // errors/warnings, or a clean toast when everything succeeded. Clean toast includes the linked count
    // so designers see at a glance that the neighborhood recompiled.
    if (bLogPageOpen)
    {
        if (TotalErrors > 0)
        {
            CompilerLog.Notify(FText::Format(NSLOCTEXT("SimpleQuestEditor", "CompileErrors", "Quest compilation: {0} error(s)"), TotalErrors));
        }
        else if (TotalWarnings > 0)
        {
            CompilerLog.Notify(FText::Format(NSLOCTEXT("SimpleQuestEditor", "CompileWarnings", "Quest compilation: {0} warning(s)"), TotalWarnings));
        }
    }
    else
    {
        const FText SuccessText = (NeighborSuccessCount > 0)
            ? FText::Format(NSLOCTEXT("SimpleQuestEditor", "CompileSuccessWithLinked",
                "Questline compiled successfully. {0} linked graph(s) also compiled."), NeighborSuccessCount)
            : NSLOCTEXT("SimpleQuestEditor", "CompileSuccess", "Questline compiled successfully.");

        FNotificationInfo Info(SuccessText);
        Info.ExpireDuration = 3.f;
        Info.bUseSuccessFailIcons = true;
        FSlateNotificationManager::Get().AddNotification(Info)->SetCompletionState(SNotificationItem::CS_Success);
    }

    if (AllRenames.Num() > 0)
    {
        FNotificationInfo RenameInfo(FText::Format(
            NSLOCTEXT("SimpleQuestEditor", "TagRenames",
                "{0} tag(s) renamed. {1} actor(s) updated in loaded levels."),
            AllRenames.Num(), TotalRenamedActors));
        RenameInfo.ExpireDuration = 5.f;
        RenameInfo.bUseSuccessFailIcons = true;
        FSlateNotificationManager::Get().AddNotification(RenameInfo)->SetCompletionState(SNotificationItem::CS_Success);
    }
}

void FQuestlineGraphEditor::ValidatePrereqTags()
{
    FSimpleQuestEditorUtilities::FQuestTagValidationResult Result = FSimpleQuestEditorUtilities::ValidateProjectPrereqTags();

    FMessageLog ValidatorLog("QuestValidator");
    ValidatorLog.NewPage(FText::Format(
        NSLOCTEXT("SimpleQuestEditor", "ValidatePageLabel", "Validate: {0}"),
        FText::FromString(FDateTime::Now().ToString())));

    for (const FSimpleQuestEditorUtilities::FQuestTagValidationDiagnostic& Diag : Result.Diagnostics)
    {
        ValidatorLog.AddMessage(Diag.Message);
    }

    if (Result.ErrorCount + Result.WarningCount > 0)
    {
        ValidatorLog.Notify(FText::Format(
            NSLOCTEXT("SimpleQuestEditor", "ValidateFoundIssues",
                "Tag validation: {0} error(s), {1} warning(s)"),
            Result.ErrorCount, Result.WarningCount));
    }
    else
    {
        FNotificationInfo Info(NSLOCTEXT("SimpleQuestEditor", "ValidateClean",
            "Tag validation: no broken references found."));
        Info.ExpireDuration = 3.f;
        Info.bUseSuccessFailIcons = true;
        FSlateNotificationManager::Get().AddNotification(Info)->SetCompletionState(SNotificationItem::CS_Success);
    }
}

void FQuestlineGraphEditor::SaveAsset_Execute()
{
    CompileQuestlineGraph();
    FAssetEditorToolkit::SaveAsset_Execute();
}

void FQuestlineGraphEditor::ExtendToolbar()
{
    TSharedPtr<FExtender> ToolbarExtender = MakeShared<FExtender>();
    ToolbarExtender->AddToolBarExtension(
        "Asset",
        EExtensionHook::After,
        GraphEditorCommands,
        FToolBarExtensionDelegate::CreateSP(this, &FQuestlineGraphEditor::FillToolbar));
    AddToolbarExtender(ToolbarExtender);
}

void FQuestlineGraphEditor::FillToolbar(FToolBarBuilder& ToolbarBuilder)
{
    ToolbarBuilder.BeginSection("Compile");

    // Main compile button — compiles current graph, with status icon
    ToolbarBuilder.AddToolBarButton(
        FQuestlineGraphEditorCommands::Get().CompileQuestlineGraph,
        NAME_None,
        TAttribute<FText>(),
        TAttribute<FText>(),
        TAttribute<FSlateIcon>::CreateLambda([this]() -> FSlateIcon
        {
            return GetCompileStatusIcon();
        }));

    // Compile options dropdown (Save on Compile, Jump to Error — future)
    ToolbarBuilder.AddComboButton(
        FUIAction(),
        FOnGetContent::CreateSP(this, &FQuestlineGraphEditor::GenerateCompileOptionsMenu),
        TAttribute<FText>(),
        NSLOCTEXT("SimpleQuestEditor", "CompileOptions_Tooltip", "Compile options"),
        TAttribute<FSlateIcon>(),
        true);

    // Compile All — dedicated button
    ToolbarBuilder.AddToolBarButton(
        FQuestlineGraphEditorCommands::Get().CompileAllQuestlineGraphs,
        NAME_None,
        NSLOCTEXT("SimpleQuestEditor", "CompileAll_Label", "All"),
        NSLOCTEXT("SimpleQuestEditor", "CompileAll_Tooltip", "Compile and save every questline graph in the project"),
        FSlateIcon(FAppStyle::GetAppStyleSetName(), "Blueprint.CompileStatus.Background"));

    ToolbarBuilder.EndSection();

    ToolbarBuilder.BeginSection("Resolver");

    ToolbarBuilder.AddToolBarButton(
        FQuestlineGraphEditorCommands::Get().OpenSourceData,
        NAME_None,
        NSLOCTEXT("SimpleQuestEditor", "SourceData_Label", "Source Data"),
        TAttribute<FText>(),
        FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Search"));

    ToolbarBuilder.EndSection();

    ToolbarBuilder.BeginSection("GraphDefaults");

    // Graph Defaults — pins the Details panel to the asset's own properties. Toggle button, mirrors BP's Class Defaults.
    ToolbarBuilder.AddToolBarButton(
        FQuestlineGraphEditorCommands::Get().ToggleGraphDefaults,
        NAME_None,
        TAttribute<FText>(),
        TAttribute<FText>(),
        FSlateIcon(FAppStyle::GetAppStyleSetName(), "FullBlueprintEditor.EditClassDefaults"));

    ToolbarBuilder.EndSection();

    ToolbarBuilder.BeginSection("Validate");

    // Validate Tags — project-wide scan for broken prereq leaf / rule references + unused Rule Entries.
    ToolbarBuilder.AddToolBarButton(
        FQuestlineGraphEditorCommands::Get().ValidatePrereqTags,
        NAME_None,
        TAttribute<FText>(),
        TAttribute<FText>(),
        FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Search"));

    ToolbarBuilder.EndSection();
}

TSharedRef<SWidget> FQuestlineGraphEditor::GenerateCompileOptionsMenu()
{
    FMenuBuilder MenuBuilder(true, GraphEditorCommands);

    // Placeholder for future options:
    // MenuBuilder.BeginSection("CompileSettings", LOCTEXT("CompileSettings", "Settings"));
    // MenuBuilder.AddSubMenu(..., "Save on Compile", ...);
    // MenuBuilder.AddMenuEntry(JumpToErrorNode);
    // MenuBuilder.EndSection();

    MenuBuilder.BeginSection("CompileInfo");
    MenuBuilder.AddMenuEntry(NSLOCTEXT("SimpleQuestEditor", "CompileOptions_Placeholder", "No options yet"),
        FText(), FSlateIcon(), FUIAction(), NAME_None, EUserInterfaceActionType::None);
    MenuBuilder.EndSection();

    return MenuBuilder.MakeWidget();
}

void FQuestlineGraphEditor::OnExternalCompile(const FString& PackagePath, bool bSuccess)
{
    if (!QuestlineGraph) return;

    const bool bIsOwnAsset = (QuestlineGraph->GetOutermost()->GetName() == PackagePath);

    // Any successful compile (this asset OR any other) may change the state this editor displays — contextual
    // givers in particular pull from other assets' CompiledQuestTags AR entries. Refresh unconditionally on
    // success so node widgets re-query and contextual entries resync without a close-and-reopen workaround.
    if (bSuccess) RefreshAllNodeWidgets();

    // Compile status and outliner are this-editor-specific — only update for OUR asset.
    if (bIsOwnAsset)
    {
        CompileStatus = bSuccess ? EQuestlineCompileStatus::UpToDate : EQuestlineCompileStatus::Error;
        if (bSuccess && OutlinerPanel.IsValid()) OutlinerPanel->Refresh();
    }
}

static void RefreshNodeWidgetsRecursive(UEdGraph* Graph)
{
    if (!Graph) return;
    Graph->NotifyGraphChanged();
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        if (UQuestlineNode_Quest* QuestNode = Cast<UQuestlineNode_Quest>(Node))
        {
            if (UEdGraph* InnerGraph = QuestNode->GetInnerGraph())
            {
                RefreshNodeWidgetsRecursive(InnerGraph);
            }
        }
    }
    // LinkedQuestline's linked asset is handled by that asset's own editor (if open) via the OnQuestlineCompiled
    // broadcast to OnExternalCompile path. Don't recurse into it here.
}

void FQuestlineGraphEditor::RefreshAllNodeWidgets()
{
    if (!QuestlineGraph) return;
    // The recursive walker calls NotifyGraphChanged on every graph, which fires OnGraphChanged. Guard the
    // dirty-reset across the refresh: compile-triggered refreshes (including neighbor-asset broadcasts during
    // auto-compile-linked fan-out) are not user edits and shouldn't drop the status icon to Unknown.
    TGuardValue<bool> Guard(bSuppressDirtyOnGraphChange, true);
    RefreshNodeWidgetsRecursive(QuestlineGraph->QuestlineEdGraph);
}

FText FQuestlineGraphEditor::GetGraphDisplayName(UEdGraph* Graph) const
{
    if (Graph == QuestlineGraph->QuestlineEdGraph) return FText::FromString(QuestlineGraph->GetName());

    // Inner graph — find the Quest node that owns it
    if (UObject* Outer = Graph->GetOuter())
    {
        if (UQuestlineNode_Quest* QuestNode = Cast<UQuestlineNode_Quest>(Outer))
        {
            return QuestNode->GetNodeTitle(ENodeTitleType::FullTitle);
        }
    }
    return FText::FromString(Graph->GetName());
}

void FQuestlineGraphEditor::OnGraphChanged(const FEdGraphEditAction&)
{
    // Refreshes from compile broadcasts shouldn't drop the status back to Unknown — a user-driven graph edit
    // should. RefreshAllNodeWidgets sets bSuppressDirtyOnGraphChange to distinguish the two call origins.
    if (bSuppressDirtyOnGraphChange) return;
    CompileStatus = EQuestlineCompileStatus::Unknown;
}

FSlateIcon FQuestlineGraphEditor::GetCompileStatusIcon() const
{
    static const FName Background("Blueprint.CompileStatus.Background");
    static const FName Unknown("Blueprint.CompileStatus.Overlay.Unknown");
    static const FName Good("Blueprint.CompileStatus.Overlay.Good");
    static const FName Error("Blueprint.CompileStatus.Overlay.Error");

    switch (CompileStatus)
    {
    case EQuestlineCompileStatus::UpToDate:
        return FSlateIcon(FAppStyle::GetAppStyleSetName(), Background, NAME_None, Good);
    case EQuestlineCompileStatus::Error:
        return FSlateIcon(FAppStyle::GetAppStyleSetName(), Background, NAME_None, Error);
    default:
        return FSlateIcon(FAppStyle::GetAppStyleSetName(), Background, NAME_None, Unknown);
    }
}


void FQuestlineGraphEditor::OnNodeTitleCommitted(const FText& NewText, ETextCommit::Type CommitType, UEdGraphNode* NodeBeingChanged)
{
    if (CommitType == ETextCommit::OnCleared) return;
    if (!NodeBeingChanged || !NodeBeingChanged->GetCanRenameNode()) return;

    // Reject collisions with sibling live labels or compiled identities BEFORE applying the rename. The compile-time
    // redirect machinery can't gracefully resolve two nodes claiming the same name in one compile cycle — the cleanest
    // outcome there is to drop one of the renames, which orphans that node's subscribers. Catching the collision here
    // surfaces the situation to the designer with a clear path forward (recompile, then rename).
    if (UQuestlineNode_ContentBase* ContentNode = Cast<UQuestlineNode_ContentBase>(NodeBeingChanged))
    {
        FText ErrorText;
        if (!ContentNode->IsLabelAvailable(NewText.ToString(), ErrorText))
        {
            FNotificationInfo Info(ErrorText);
            Info.ExpireDuration = 6.f;
            Info.bUseSuccessFailIcons = true;
            FSlateNotificationManager::Get().AddNotification(Info)->SetCompletionState(SNotificationItem::CS_Fail);
            return;
        }
    }

    const FScopedTransaction Transaction(NSLOCTEXT("SimpleQuestEditor", "RenameNode", "Rename Node"));
    NodeBeingChanged->Modify();
    NodeBeingChanged->OnRenameNode(NewText.ToString());
}

TSharedRef<SDockTab> FQuestlineGraphEditor::SpawnDetailsTab(const FSpawnTabArgs& Args)
{
    return SNew(SDockTab)
        .Label(NSLOCTEXT("SimpleQuestEditor", "DetailsTabLabel", "Details"))
        [
            DetailsView.IsValid() ? DetailsView.ToSharedRef() : SNullWidget::NullWidget
        ];
}

void FQuestlineGraphEditor::OnGraphSelectionChanged(const FGraphPanelSelectionSet& SelectedNodes)
{
    if (!DetailsView.IsValid()) return;

    // Graph Defaults pinned — selection changes are ignored; Details stays locked on the asset. Mirror of BP's
    // Class Defaults button.
    if (bGraphDefaultsPinned)
    {
        if (QuestlineGraph) DetailsView->SetObject(QuestlineGraph);
        return;
    }

    // Empty selection — restore the asset view so graph-level metadata (QuestlineID, DisplayName) stays reachable
    // without forcing the designer to go through content browser → Properties for every edit.
    if (SelectedNodes.IsEmpty())
    {
        if (QuestlineGraph)
        {
            DetailsView->SetObject(QuestlineGraph);
        }
        else
        {
            DetailsView->SetObjects(TArray<UObject*>{});
        }
        return;
    }

    TArray<UObject*> Selected;
    for (UObject* Obj : SelectedNodes)
        Selected.Add(Obj);

    DetailsView->SetObjects(Selected);
}

void FQuestlineGraphEditor::ToggleGraphDefaults()
{
    bGraphDefaultsPinned = !bGraphDefaultsPinned;

    if (!DetailsView.IsValid()) return;

    if (bGraphDefaultsPinned)
    {
        // Pin — swap Details to the asset regardless of current selection.
        if (QuestlineGraph) DetailsView->SetObject(QuestlineGraph);
    }
    else if (GraphEditorWidget.IsValid())
    {
        // Unpin — re-run selection logic so whatever is selected takes over; empty fall-back puts the asset back.
        OnGraphSelectionChanged(GraphEditorWidget->GetGraphEditor()->GetSelectedNodes());
    }
}

TSharedRef<SDockTab> FQuestlineGraphEditor::SpawnOutlinerTab(const FSpawnTabArgs& Args)
{
    OutlinerPanel = SNew(SQuestlineOutlinerPanel, QuestlineGraph)
        .OnItemNavigate(this, &FQuestlineGraphEditor::OnOutlinerItemNavigate);

    return SNew(SDockTab)
        .Label(NSLOCTEXT("SimpleQuestEditor", "OutlinerTabLabel", "Questline Outliner"))
        [
            OutlinerPanel.ToSharedRef()
        ];
}

TSharedRef<SDockTab> FQuestlineGraphEditor::SpawnGroupExaminerTab(const FSpawnTabArgs& Args)
{
    if (!GroupExaminerPanel.IsValid())
    {
        GroupExaminerPanel = SNew(SGroupExaminerPanel);
    }

    return SNew(SDockTab)
        .Label(NSLOCTEXT("SimpleQuestEditor", "GroupExaminerTabLabel", "Group Examiner"))
        [
            GroupExaminerPanel.ToSharedRef()
        ];
}

TSharedRef<SDockTab> FQuestlineGraphEditor::SpawnPlanTab(const FSpawnTabArgs& Args)
{
    if (!PlanPanel.IsValid())
    {
        // Bound to the asset path the resolver keys plans by, so a plan computed for a DIFFERENT questline never
        // renders here. GetPathName gives the same spelling the console command resolves --in-place to.
        PlanPanel = SNew(SQuestPlanPanel)
            .TargetAssetPath(QuestlineGraph ? QuestlineGraph->GetPathName() : FString())
            .Questline(QuestlineGraph)
            .OnBuildPlanRequested(FSimpleDelegate::CreateSP(this, &FQuestlineGraphEditor::BuildImportPlan))
            .OnApplyRequested(FSimpleDelegate::CreateSP(this, &FQuestlineGraphEditor::ApplyImportPlan))
            .OnBrowseRequested(FSimpleDelegate::CreateSP(this, &FQuestlineGraphEditor::BrowseForImportFolder))
            .CanBuildPlan(TAttribute<bool>::CreateSP(this, &FQuestlineGraphEditor::CanBuildImportPlan))
            .CanApply(TAttribute<bool>::CreateSP(this, &FQuestlineGraphEditor::CanApplyImportPlan))
            .SourceStale(TAttribute<bool>::CreateSP(this, &FQuestlineGraphEditor::IsPlanSourceStale))
            .PlanProvenance(TAttribute<FText>::CreateSP(this, &FQuestlineGraphEditor::GetPlanProvenanceLabel))
            .SourceFolder(TAttribute<FString>::CreateSP(this, &FQuestlineGraphEditor::GetImportFolder))
            .OnFolderChanged(FOnQuestPlanFolderChanged::CreateSP(this, &FQuestlineGraphEditor::HandleImportFolderChanged))
            .SourceTable(TAttribute<FSoftObjectPath>::CreateSP(this, &FQuestlineGraphEditor::GetImportTable))
            .OnTableChanged(FOnQuestPlanTableChanged::CreateSP(this, &FQuestlineGraphEditor::HandleImportTableChanged))
            .OnSourceKindChanged(FOnQuestPlanSourceKindChanged::CreateSP(this, &FQuestlineGraphEditor::HandleSourceKindChanged))
            .FormatName(TAttribute<FString>::CreateSP(this, &FQuestlineGraphEditor::GetImportFormatName))
            .OnFormatChanged(FOnQuestPlanFormatChanged::CreateSP(this, &FQuestlineGraphEditor::HandleImportFormatChanged))
            .MappingAsset(TAttribute<FSoftObjectPath>::CreateSP(this, &FQuestlineGraphEditor::GetImportMappingPath))
            .OnMappingChanged(FOnQuestPlanMappingChanged::CreateSP(this, &FQuestlineGraphEditor::HandleImportMappingChanged))
            .OnExportRequested(FSimpleDelegate::CreateSP(this, &FQuestlineGraphEditor::ExportQuestlineData))
            .CanExport(TAttribute<bool>::CreateSP(this, &FQuestlineGraphEditor::CanExportQuestlineData))
            .DestinationFolder(TAttribute<FString>::CreateSP(this, &FQuestlineGraphEditor::GetExportFolder))
            .OnDestinationFolderChanged(FOnQuestPlanFolderChanged::CreateSP(this, &FQuestlineGraphEditor::HandleExportFolderChanged))
            .DestinationTable(TAttribute<FSoftObjectPath>::CreateSP(this, &FQuestlineGraphEditor::GetExportTable))
            .OnDestinationTableChanged(FOnQuestPlanTableChanged::CreateSP(this, &FQuestlineGraphEditor::HandleExportTableChanged))
            .OnDestinationKindChanged(FOnQuestPlanSourceKindChanged::CreateSP(this, &FQuestlineGraphEditor::HandleDestinationKindChanged))
            .DestinationFormatName(TAttribute<FString>::CreateSP(this, &FQuestlineGraphEditor::GetExportFormatName))
            .OnDestinationFormatChanged(FOnQuestPlanFormatChanged::CreateSP(this, &FQuestlineGraphEditor::HandleExportFormatChanged))
            .DestinationMapping(TAttribute<FSoftObjectPath>::CreateSP(this, &FQuestlineGraphEditor::GetExportMappingPath))
            .OnDestinationMappingChanged(FOnQuestPlanMappingChanged::CreateSP(this, &FQuestlineGraphEditor::HandleExportMappingChanged))
            .OnDestinationBrowseRequested(FSimpleDelegate::CreateSP(this, &FQuestlineGraphEditor::BrowseForExportFolder));
    }

    return SNew(SDockTab)
        .Label(NSLOCTEXT("SimpleQuestEditor", "SourceDataTabLabel", "Source Data"))
        [
            PlanPanel.ToSharedRef()
        ];
}

bool FQuestlineGraphEditor::RunImport(const FQuestPlanSource& Source, bool bApply)
{
    if (!QuestlineGraph || !QuestlineGraph->QuestlineEdGraph) { return false; }

    // From the SOURCE, not the held selection: an Apply re-running a reviewed plan must use the mapping that plan was
    // built with, even if the picker has moved since.
    const UQuestImportMapping* Mapping = Cast<UQuestImportMapping>(Source.Mapping.TryLoad());

    FQuestImportRequest Request;
    // Kind comes from the source rather than being assumed. Hardcoding ForeignFile here is what made a DataTable plan
    // un-runnable from this editor even when the record it came from named the table perfectly well.
    Request.Endpoint = QuestEndpointFromPlanSource(Source);
    Request.Mapping = Mapping;
    Request.Policies = QuestImport_ResolvePolicies(Mapping, false);

    // Defaulted at the USE site as well as at init, because a format is only ever absent by accident - and the error it
    // produces names the folder, which is the one thing the user did supply.
    if (Request.Endpoint.Kind == EQuestEndpointKind::ForeignFile && Request.Endpoint.FormatName.IsEmpty())
    {
        Request.Endpoint.FormatName = TEXT("TSV");
    }

    // The caller owns the transaction, so an apply driven from the toolbar is ONE undo step covering everything the
    // plan performs - the same guarantee the console gives. Held by SCOPE, not by use: constructing it opens the
    // transaction and this pointer's destruction closes it, which is why nothing below refers to it again. Conditional,
    // so a read-only plan opens no transaction at all.
    TUniquePtr<FScopedTransaction> Transaction;
    if (bApply)
    {
        Transaction = MakeUnique<FScopedTransaction>(
            NSLOCTEXT("SimpleQuestEditor", "ApplyImportPlanTransaction", "Apply Import Plan"));
    }

    FQuestImportOutcome Outcome;
    if (!QuestImport_RunInPlace(*QuestlineGraph, Request, bApply, Outcome))
    {
        UE_LOG(LogSimpleQuestResolver, Error, TEXT("Build Plan: %s. Nothing was modified."), *Outcome.Error);

        // Published as well as logged. Picking the wrong format is now one click, and a failure that only reaches the
        // log leaves the panel saying "no plan has been computed" - which reads as "try again" for the thing that just
        // failed. The notification catches the eye; the panel is where the reason stays.
        FQuestPlanBroker::Get().PublishFailure(QuestlineGraph->GetPathName(), Outcome.Error, LastImportSource);

        FNotificationInfo Info(FText::Format(
            NSLOCTEXT("SimpleQuestEditor", "ImportReadFailed", "Could not read the source — {0}"),
            FText::FromString(Outcome.Error)));
        Info.ExpireDuration = 6.f;
        Info.bUseSuccessFailIcons = true;
        FSlateNotificationManager::Get().AddNotification(Info)->SetCompletionState(SNotificationItem::CS_Fail);
        return false;
    }

    Outcome.Plan.TargetAssetPath = QuestlineGraph->GetPathName();

    UE_LOG(LogSimpleQuestResolver, Log, TEXT("%s: %s"),
        bApply ? TEXT("Apply Plan") : TEXT("Build Plan"),
        *BuildQuestPlanSummary(Outcome.Plan, EQuestPlanSubject::Questline));
    
    FQuestPlanBroker::Get().Publish(Outcome.Plan.TargetAssetPath, Outcome.Plan, Source);

    // An APPLIED plan describes the past, so it goes once it has run - keeping it leaves Apply lit over work already
    // done. Published FIRST and cleared after, rather than skipped: an apply that REFUSES has not run, and the plan
    // together with its refusals is precisely what should still be on screen.
    if (bApply && !Outcome.ApplyResult.bRefused)
    {
        FQuestPlanBroker::Get().Clear(Outcome.Plan.TargetAssetPath);
    }
    
    // An apply recompiles the target and its linked neighborhood (QuestImport_RunInPlace owns that, so the console gets
    // it too). Report the outcome here rather than only logging it: this path has a designer watching, and "the change
    // landed but the asset still needs compiling" is precisely the state they must not walk away from.
    if (bApply && Outcome.ApplyResult.GraphsCompiled > 0)
    {
        if (Outcome.ApplyResult.bCompileSucceeded)
        {
            FNotificationInfo Info(FText::Format(
                NSLOCTEXT("SimpleQuestEditor", "ApplyRecompiled", "Import applied. {0} graph(s) recompiled."),
                Outcome.ApplyResult.GraphsCompiled));
            Info.ExpireDuration = 3.f;
            Info.bUseSuccessFailIcons = true;
            FSlateNotificationManager::Get().AddNotification(Info)->SetCompletionState(SNotificationItem::CS_Success);
        }
        else
        {
            // Deliberately NOT rolled back — the writes were correct and reverting them would discard good work to
            // report a compile problem. The asset is modified and uncompiled, which is a state the compiler log can
            // explain and a recompile can clear.
            UE_LOG(LogSimpleQuestResolver, Error, TEXT("Apply Plan: the import applied but a recompile FAILED - '%s' is "
                "modified and needs a manual compile."), *QuestlineGraph->GetPathName());

            FNotificationInfo Info(NSLOCTEXT("SimpleQuestEditor", "ApplyCompileFailed",
                "Import applied, but the recompile failed. The questline is modified and needs compiling — see the Quest Compiler log."));
            Info.ExpireDuration = 8.f;
            Info.bUseSuccessFailIcons = true;
            FSlateNotificationManager::Get().AddNotification(Info)->SetCompletionState(SNotificationItem::CS_Fail);
        }
    }
    return true;
}

void FQuestlineGraphEditor::OpenSourceData()
{
    if (TabManager.IsValid()) { TabManager->TryInvokeTab(PlanTabId); }
}

void FQuestlineGraphEditor::RestoreImportEndpointFromMemo()
{
    if (!QuestlineGraph) { return; }
    const UQuestResolverEditorMemo* Memo = UQuestResolverEditorMemo::Get();
    const FString Path = QuestlineGraph->GetPathName();

    if (const FQuestResolverEndpointMemo* Remembered = Memo->EndpointByQuestline.Find(Path))
    {
        // Each field is taken only when it says something, so a memo written by an older build - or one whose format
        // was never chosen - leaves the seeded default standing rather than blanking it.
        LastImportSource.Folder = Remembered->Folder;
        if (!Remembered->FormatName.IsEmpty()) { LastImportSource.FormatName = Remembered->FormatName; }
        LastImportSource.Table = FSoftObjectPath(Remembered->Table);
        LastImportSource.Mapping = FSoftObjectPath(Remembered->Mapping);
    }

    if (const FQuestResolverEndpointMemo* Dest = Memo->DestinationByQuestline.Find(Path))
    {
        LastExportDestination.Folder = Dest->Folder;
        if (!Dest->FormatName.IsEmpty()) { LastExportDestination.FormatName = Dest->FormatName; }
        LastExportDestination.Table = FSoftObjectPath(Dest->Table);
        LastExportDestination.Mapping = FSoftObjectPath(Dest->Mapping);
    }
    else if (const FQuestResolverEndpointMemo* Remembered = Memo->EndpointByQuestline.Find(Path))
    {
        // MIGRATION, once. A questline remembered before the split has no destination memo, and export used to read the
        // SOURCE's folder and format - so seeding from those puts it back exactly where it used to write. The TABLE is
        // deliberately not carried across: export refused a table outright, so no questline ever wrote to one, and
        // inheriting it would silently turn a folder export into a write into the table it was imported from.
        LastExportDestination.Folder = Remembered->Folder;
        if (!Remembered->FormatName.IsEmpty()) { LastExportDestination.FormatName = Remembered->FormatName; }
        LastExportDestination.Mapping = FSoftObjectPath(Remembered->Mapping);
    }
}

void FQuestlineGraphEditor::SaveImportEndpointToMemo() const
{
    if (!QuestlineGraph) { return; }
    const FString Path = QuestlineGraph->GetPathName();

    FQuestResolverEndpointMemo Source;
    Source.Folder     = LastImportSource.Folder;
    Source.FormatName = LastImportSource.FormatName;
    Source.Table      = LastImportSource.Table.ToString();
    Source.Mapping    = LastImportSource.Mapping.ToString();

    FQuestResolverEndpointMemo Destination;
    Destination.Folder     = LastExportDestination.Folder;
    Destination.FormatName = LastExportDestination.FormatName;
    Destination.Table      = LastExportDestination.Table.ToString();
    Destination.Mapping    = LastExportDestination.Mapping.ToString();

    UQuestResolverEditorMemo* Memo = UQuestResolverEditorMemo::Get();
    Memo->EndpointByQuestline.Add(Path, Source);
    Memo->DestinationByQuestline.Add(Path, Destination);
    Memo->SaveConfig();
}

void FQuestlineGraphEditor::BrowseForImportFolder()
{
    IDesktopPlatform* Desktop = FDesktopPlatformModule::Get();
    if (!Desktop) { return; }

    const void* ParentWindow = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);
    FString Chosen;
    if (!Desktop->OpenDirectoryDialog(ParentWindow,
            NSLOCTEXT("SimpleQuestEditor", "ChooseSourceTitle", "Choose a folder of source data").ToString(),
            LastImportSource.Folder, Chosen))
    {
        return;   // cancelled; nothing to say
    }

    // Routed through the same write typing uses, so browsing and typing cannot diverge.
    HandleImportFolderChanged(Chosen);
}

void FQuestlineGraphEditor::HandleImportFolderChanged(const FString& NewFolder)
{
    if (NewFolder == LastImportSource.Folder) { return; }
    LastImportSource.Folder = NewFolder;
    // The two provenances are exclusive; naming a folder means this is no longer a table source.
    LastImportSource.Table.Reset();
    SaveImportEndpointToMemo();
}

void FQuestlineGraphEditor::HandleImportTableChanged(const FSoftObjectPath& NewTable)
{
    if (NewTable == LastImportSource.Table) { return; }
    LastImportSource.Table = NewTable;
    LastImportSource.Folder.Reset();
    SaveImportEndpointToMemo();
}

void FQuestlineGraphEditor::HandleSourceKindChanged(EQuestPlanSourceKind NewKind)
{
    // Clears the side no longer in play rather than leaving both set, so the endpoint's derived kind always agrees with
    // what the panel is showing. The panel keeps its own notion of which control is visible, because it can sit on
    // Data Table with nothing picked - a state no source can hold.
    if (NewKind == EQuestPlanSourceKind::DataTable) { LastImportSource.Folder.Reset(); }
    else                                            { LastImportSource.Table.Reset(); }
    SaveImportEndpointToMemo();
}

void FQuestlineGraphEditor::HandleImportFormatChanged(FString NewFormat)
{
    if (NewFormat == LastImportSource.FormatName) { return; }
    LastImportSource.FormatName = MoveTemp(NewFormat);
    SaveImportEndpointToMemo();
}

void FQuestlineGraphEditor::HandleImportMappingChanged(const FSoftObjectPath& NewMapping)
{
    if (NewMapping == LastImportSource.Mapping) { return; }
    LastImportSource.Mapping = NewMapping;
    SaveImportEndpointToMemo();
}

// The destination half. Deliberately the same shape as the import handlers above rather than something cleverer: they
// answer the same questions about a different endpoint, and a reader who has understood one has understood both.
void FQuestlineGraphEditor::HandleExportFolderChanged(const FString& NewFolder)
{
    if (NewFolder == LastExportDestination.Folder) { return; }
    LastExportDestination.Folder = NewFolder;
    // The two provenances are exclusive; naming a folder means this is no longer a table destination.
    LastExportDestination.Table.Reset();
    SaveImportEndpointToMemo();
}

void FQuestlineGraphEditor::HandleExportTableChanged(const FSoftObjectPath& NewTable)
{
    if (NewTable == LastExportDestination.Table) { return; }
    LastExportDestination.Table = NewTable;
    LastExportDestination.Folder.Reset();
    SaveImportEndpointToMemo();
}

void FQuestlineGraphEditor::HandleDestinationKindChanged(EQuestPlanSourceKind NewKind)
{
    if (NewKind == EQuestPlanSourceKind::DataTable) { LastExportDestination.Folder.Reset(); }
    else                                            { LastExportDestination.Table.Reset(); }
    SaveImportEndpointToMemo();
}

void FQuestlineGraphEditor::HandleExportFormatChanged(FString NewFormat)
{
    if (NewFormat == LastExportDestination.FormatName) { return; }
    LastExportDestination.FormatName = MoveTemp(NewFormat);
    SaveImportEndpointToMemo();
}

void FQuestlineGraphEditor::HandleExportMappingChanged(const FSoftObjectPath& NewMapping)
{
    if (NewMapping == LastExportDestination.Mapping) { return; }
    LastExportDestination.Mapping = NewMapping;
    SaveImportEndpointToMemo();
}

void FQuestlineGraphEditor::BrowseForExportFolder()
{
    IDesktopPlatform* Desktop = FDesktopPlatformModule::Get();
    if (!Desktop) { return; }

    const void* ParentWindow = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);
    FString Chosen;
    if (!Desktop->OpenDirectoryDialog(ParentWindow,
            NSLOCTEXT("SimpleQuestEditor", "ChooseDestinationTitle", "Choose a folder to write this questline into").ToString(),
            LastExportDestination.Folder, Chosen))
    {
        return;   // cancelled; nothing to say
    }

    // Routed through the same write typing uses, so browsing and typing cannot diverge.
    HandleExportFolderChanged(Chosen);
}

FText FQuestlineGraphEditor::GetPlanProvenanceLabel() const
{
    if (!QuestlineGraph) { return FText::GetEmpty(); }

    // ALWAYS says something. A line that collapses when there is no plan makes the row jump, and its absence carries
    // no information - "there is no plan" is a fact worth stating plainly.
    const FQuestPlanRecord* Record = FQuestPlanBroker::Get().Find(QuestlineGraph->GetPathName());
    if (!Record || !Record->Source.IsValid())
    {
        return NSLOCTEXT("SimpleQuestEditor", "PlanNone", "No plan built");
    }
    // A FAILURE record carries a source as well, so testing Source.IsValid() alone announced "Plan built from ..." for
    // a read that produced nothing. The reason belongs to the summary; this line only names which state we are in.
    if (!Record->Error.IsEmpty())
    {
        return NSLOCTEXT("SimpleQuestEditor", "PlanReadFailed", "No plan — the last read failed");
    }

    // Names what the ROWS are a statement about, which is no longer the same fact as what the fields above show. The
    // format is part of it: one folder read as TSV and the same folder read as JSON are different sources.
    return Record->Source.Kind() == EQuestPlanSourceKind::DataTable
        ? FText::Format(NSLOCTEXT("SimpleQuestEditor", "PlanFromTable", "Plan built from {0}"),
            FText::FromString(Record->Source.Table.ToString()))
        : FText::Format(NSLOCTEXT("SimpleQuestEditor", "PlanFromFolder", "Plan built from {0} as {1}"),
            FText::FromString(Record->Source.Folder),
            FText::FromString(Record->Source.FormatName));
}

bool FQuestlineGraphEditor::IsPlanSourceStale() const
{
    if (!QuestlineGraph) { return false; }
    const FQuestPlanRecord* Record = FQuestPlanBroker::Get().Find(QuestlineGraph->GetPathName());
    // A failure record describes no plan, so there is nothing for the selection to have drifted away from.
    if (!Record || !Record->Source.IsValid() || !Record->Error.IsEmpty()) { return false; }

    // WHICH SELECTION a plan is answerable to depends on WHICH WAY IT POINTS. FQuestPlanRecord::Source means "what
    // this plan was built from", and for an OUTBOUND plan that is the DESTINATION endpoint - so comparing it against
    // the import row reported every export plan stale the moment it was built, before anything had changed. The field
    // name says Source while the meaning is provenance; until that name is revisited, direction has to be read here.
    const FQuestPlanSource& Selection = (Record->Plan.Direction == EQuestPlanDirection::IntoTable)
        ? LastExportDestination
        : LastImportSource;

    if (Record->Source.Mapping != Selection.Mapping) { return true; }
    if (Record->Source.Kind() != Selection.Kind()) { return true; }
    // Compared per field rather than by whole-struct equality, because FormatName is meaningless for a table and would
    // otherwise report every table plan stale the moment the combo held anything.
    if (Selection.Kind() == EQuestPlanSourceKind::DataTable)
    {
        return Record->Source.Table != Selection.Table;
    }
    return Record->Source.Folder != Selection.Folder
        || Record->Source.FormatName != Selection.FormatName;
}

void FQuestlineGraphEditor::ApplyImportPlan()
{
    if (!QuestlineGraph) { return; }
    const FQuestPlanRecord* Record = FQuestPlanBroker::Get().Find(QuestlineGraph->GetPathName());
    if (!Record || !Record->Source.IsValid()) { return; }

    // ONE Apply, dispatching on the plan itself. This is what putting Direction on the PLAN rather than the broker
    // record buys: the button does not need to know which endpoint field was last edited, only what it is holding.
    //
    // Both arms re-run the RECORD's source rather than the current selection - "what runs is what you reviewed" is the
    // promise, and the two can legitimately differ once a designer edits a field after building a plan. Neither writes
    // LastImportSource, for the same reason.
    if (Record->Plan.Direction == EQuestPlanDirection::IntoTable)
    {
        ApplyRowPlan(Record->Source);
        return;
    }
    RunImport(Record->Source, true);
}

/**
 * A last look before something outside this questline changes. Defaults to NO when unattended, so a commandlet or an
 * automation run never silently performs a write a human would have been asked about - the default has to be the safe
 * answer, because nobody is there to give the unsafe one.
 */
static bool ConfirmMutation(const FText& Title, const FText& Message)
{
    return FMessageDialog::Open(EAppMsgCategory::Warning, EAppMsgType::YesNo, EAppReturnType::No, Message, Title)
        == EAppReturnType::Yes;
}

void FQuestlineGraphEditor::ApplyRowPlan(const FQuestPlanSource& Source)
{
    if (!QuestlineGraph) { return; }

    FQuestExportRequest Request;
    Request.Graph = QuestlineGraph;
    Request.Endpoint = QuestEndpointFromPlanSource(Source);
    Request.Mapping = Cast<UQuestImportMapping>(Source.Mapping.TryLoad());

    // Re-planned rather than replayed. The destination is an asset we do not own and nothing watches it, so the plan
    // reviewed a moment ago may already describe a table that has moved on - recomputing is the only honest way to
    // write what the CURRENT comparison says rather than what an old one did.
    FQuestExportOutcome Out;
    if (!QuestExport_Run(Request, Out) || !Out.bPlanned)
    {
        UE_LOG(LogSimpleQuestResolver, Error, TEXT("Apply: %s"),
            Out.Error.IsEmpty() ? TEXT("the plan could not be recomputed. Nothing was written.") : *Out.Error);
        return;
    }

    UDataTable* Destination = Request.Endpoint.Table.LoadSynchronous();
    if (!Destination)
    {
        UE_LOG(LogSimpleQuestResolver, Error, TEXT("Apply: the destination data table could not be loaded. Nothing written."));
        return;
    }

    TMap<FString, const FQuestDataRow*> RowsByKey;
    if (const FQuestDataTable* Content = Out.PlannedBundle.TablesByType.Find(TEXT("content")))
    {
        for (const FQuestDataRow& R : Content->Rows) { RowsByKey.Add(R.Key, &R); }
    }

    // Asked AFTER the re-plan, so the numbers are the ones about to happen rather than the ones last reviewed. A no-op
    // needs no permission - and asking for it would train the answer.
    if (Out.RowPlan.CountOf(EQuestNodePlanAction::Create) > 0 || Out.RowPlan.ChangedNodeCount() > 0)
    {
        if (!ConfirmMutation(
            NSLOCTEXT("SimpleQuestEditor", "ConfirmRowWriteTitle", "Write rows into a data table"),
            FText::Format(NSLOCTEXT("SimpleQuestEditor", "ConfirmRowWriteBody",
                "Write into '{0}':\n\n"
                "    {1} row(s) created\n"
                "    {2} row(s) updated\n"
                "    {3} row(s) left untouched - this questline claims none of them\n\n"
                "This modifies an asset this questline does not own. Undo restores the ENTIRE table to its current "
                "state, not only these rows - so any other edit made to it meanwhile would be reverted with them.\n\n"
                "Continue?"),
                FText::FromString(Destination->GetName()),
                Out.RowPlan.CountOf(EQuestNodePlanAction::Create),
                Out.RowPlan.ChangedNodeCount(),
                Out.RowPlan.UnclaimedRowCount)))
        {
            UE_LOG(LogSimpleQuestResolver, Log, TEXT("Apply: cancelled at the confirmation. Nothing written."));
            return;
        }
    }

    // The CALLER owns the transaction, so the write and everything around it undo as one unit.
    FScopedTransaction Transaction(NSLOCTEXT("SimpleQuest", "ApplyRowPlan", "Write questline rows into a data table"));
    FQuestApplyResult Result;
    ApplyQuestRowPlan(*Destination, Out.RowPlan, RowsByKey, Result);

    for (const FString& S : Result.Skipped)
    {
        UE_LOG(LogSimpleQuestResolver, Warning, TEXT("Apply: skipped %s"), *S);
    }
    if (Result.bRefused)
    {
        UE_LOG(LogSimpleQuestResolver, Error, TEXT("Apply: refused - the plan carries %d refusal(s) and %d contested key(s). "
            "Nothing was written."), Out.RowPlan.Refusals.Num(), Out.RowPlan.AmbiguousKeys.Num());

        // The re-plan is what refused, so publish IT - otherwise the panel keeps showing the older plan that did not.
        FQuestPlanBroker::Get().Publish(QuestlineGraph->GetPathName(), Out.RowPlan, Source);
        return;
    }

    if (Result.ChangedAnything()) { Destination->MarkPackageDirty(); }

    UE_LOG(LogSimpleQuestResolver, Log, TEXT("Apply: wrote into '%s' - %d row(s) created, %d field(s) written, %d skipped."),
        *Destination->GetName(), Result.EntitiesCreated, Result.PropertiesWritten, Result.Skipped.Num());

    // Same reason as the inbound apply: the destination has moved, so the plan now describes the past.
    FQuestPlanBroker::Get().Clear(QuestlineGraph->GetPathName());
}

void FQuestlineGraphEditor::ExportQuestlineData()
{
    if (!CanExportQuestlineData()) { return; }

    // Only the FOLDER direction asks here. A table destination PLANS - its confirmation belongs on the apply, where
    // there is an exact summary to show rather than a guess.
    if (!LastExportDestination.Table.IsValid())
    {
        const bool bDerived = LastExportDestination.Folder.IsEmpty();
        const FString Where = bDerived ? QuestExport_DerivedFolderFor(*QuestlineGraph) : LastExportDestination.Folder;

        if (!ConfirmMutation(
            NSLOCTEXT("SimpleQuestEditor", "ConfirmExportTitle", "Export questline data"),
            FText::Format(NSLOCTEXT("SimpleQuestEditor", "ConfirmExportBody",
                "Write '{0}' as {1} files into:\n\n{2}{3}\n\n"
                "Files recorded by a previous export there will be DELETED and replaced.\n\n"
                "This writes to disk, outside the project's asset system, and CANNOT BE UNDONE from the editor.\n\n"
                "Continue?"),
                FText::FromString(QuestlineGraph->GetName()),
                FText::FromString(LastExportDestination.FormatName.IsEmpty() ? TEXT("TSV") : LastExportDestination.FormatName),
                FText::FromString(Where),
                bDerived ? NSLOCTEXT("SimpleQuestEditor", "ConfirmExportDerived", "  (default destination)") : FText::GetEmpty())))
        {
            UE_LOG(LogSimpleQuestResolver, Log, TEXT("Export: cancelled at the confirmation. Nothing written."));
            return;
        }
    }

    FQuestExportRequest Request;
    Request.Graph = QuestlineGraph;
    // The DESTINATION, which is now its own memory rather than the source's folder borrowed. Kind is derived from
    // whether a table is named, so the pair can never disagree. An empty folder still means the derived destination,
    // which is what makes Export work before anyone has pointed it anywhere.
    Request.Endpoint = QuestEndpointFromPlanSource(LastExportDestination);
    Request.Mapping = Cast<UQuestImportMapping>(LastExportDestination.Mapping.TryLoad());

    FQuestExportOutcome Out;
    const bool bOk = QuestExport_Run(Request, Out);

    for (const FString& W : Out.Warnings)
    {
        UE_LOG(LogSimpleQuestResolver, Warning, TEXT("Export: %s"), *W);
    }

    if (!bOk)
    {
        UE_LOG(LogSimpleQuestResolver, Error, TEXT("Export: %s"), *Out.Error);
        FQuestPlanBroker::Get().PublishExport(QuestlineGraph->GetPathName(), FString(), Out.Error);
        return;
    }

    // A table destination PLANNED rather than wrote. Published so the panel shows it, and Apply - which dispatches on
    // the plan's direction - can act on it. Mirrors what the console does, deliberately: two surfaces, one behavior.
    if (Out.bPlanned)
    {
        FQuestPlanBroker::Get().Publish(QuestlineGraph->GetPathName(), Out.RowPlan, QuestPlanSourceFromEndpoint(Request.Endpoint, Request.Mapping));

        UE_LOG(LogSimpleQuestResolver, Log, TEXT("Plan Write: %d row(s) to create, %d to update. %d row(s) in that table "
            "are claimed by nothing here and were left alone."),
            Out.RowPlan.CountOf(EQuestNodePlanAction::Create),
            Out.RowPlan.ChangedNodeCount(),
            Out.RowPlan.UnclaimedRowCount);
        return;
    }

    const FString Summary = FString::Printf(TEXT("Exported %d file(s) to '%s'%s"),
        Out.FilesWritten,
        *Out.OutDir,
        Out.bDestinationDerived ? TEXT(" (default destination)") : TEXT(""));

    UE_LOG(LogSimpleQuestResolver, Log, TEXT("Export: '%s' - %d entity row(s) across %d type(s), %d edge(s). %s; "
        "removed %d from the previous export."),
        *Out.ExportKey, Out.EntityRows, Out.TypeCount, Out.EdgeCount, *Summary, Out.FilesRemoved);

    FQuestPlanBroker::Get().PublishExport(QuestlineGraph->GetPathName(), Summary, FString());

    // A notification as well as the panel line, because the designer who pressed the button may have the tab covered -
    // and unlike a refusal, a success does not need to persist anywhere.
    FNotificationInfo Info(FText::FromString(Summary));
    Info.ExpireDuration = 4.f;
    Info.bUseSuccessFailIcons = true;
    FSlateNotificationManager::Get().AddNotification(Info)->SetCompletionState(SNotificationItem::CS_Success);
}

bool FQuestlineGraphEditor::CanExportQuestlineData() const
{
    return QuestlineGraph != nullptr && QuestlineGraph->QuestlineEdGraph != nullptr;
}

void FQuestlineGraphEditor::BuildImportPlan()
{
    // Reads the SELECTION. Rebuild used to re-run whatever the last plan came from, which meant an edited folder could
    // not be acted on without going back through a file dialog.
    if (!CanBuildImportPlan())
    {
        // Says WHY rather than doing nothing. A greyed button explains itself only if you already know the rule, and a
        // handler that returns silently is indistinguishable from one that is not wired at all.
        UE_LOG(LogSimpleQuestResolver, Warning, TEXT("Build Plan: nothing to read - %s. Target '%s'."),
            LastImportSource.Table.IsValid() ? TEXT("no Data Table is set")
                                             : TEXT("a folder and a format are both required"),
            QuestlineGraph ? *QuestlineGraph->GetPathName() : TEXT("<none>"));
        return;
    }

    UE_LOG(LogSimpleQuestResolver, Log, TEXT("Build Plan: reading '%s'%s for '%s'."),
        LastImportSource.Table.IsValid() ? *LastImportSource.Table.ToString() : *LastImportSource.Folder,
        LastImportSource.Table.IsValid() ? TEXT("") : *FString::Printf(TEXT(" as %s"), *LastImportSource.FormatName),
        *QuestlineGraph->GetPathName());

    RunImport(LastImportSource, false);
}

bool FQuestlineGraphEditor::CanBuildImportPlan() const
{
    // A source that RESOLVES, not merely one that is non-empty: a folder needs a format to be read through, a table
    // carries its own in the row struct.
    if (!QuestlineGraph) { return false; }
    if (LastImportSource.Table.IsValid()) { return true; }
    return !LastImportSource.Folder.IsEmpty() && !LastImportSource.FormatName.IsEmpty();
}

bool FQuestlineGraphEditor::CanApplyImportPlan() const
{
    // Gated on the BROKER, not on a folder this editor session happens to remember. The panel reads the broker too, so
    // the two can no longer disagree - which they did: reopening an asset left a plan visibly displayed while Apply sat
    // greyed out, because the folder lived only in this object. A plan carrying refusals still applies nothing.
    if (!QuestlineGraph) { return false; }
    const FQuestPlanRecord* Record = FQuestPlanBroker::Get().Find(QuestlineGraph->GetPathName());
    return Record
        && Record->Source.IsValid()
        && Record->Plan.Refusals.IsEmpty()
        && Record->Plan.AmbiguousKeys.IsEmpty()
        && !Record->Plan.IsNoOp();
}

void FQuestlineGraphEditor::PinGroupExaminer(FGameplayTag GroupTag, UEdGraphNode* PinnedEndpointNode, UEdGraphNode* RowToHighlight) const
{
    // Invoke the tab; creates the panel widget via SpawnGroupExaminerTab if not already created.
    if (TabManager.IsValid())
    {
        TabManager->TryInvokeTab(GroupExaminerTabId);
    }

    if (GroupExaminerPanel.IsValid())
    {
        GroupExaminerPanel->PinGroup(GroupTag, PinnedEndpointNode);
        if (RowToHighlight)
        {
            GroupExaminerPanel->SelectRowForNode(RowToHighlight);
        }
    }
}

void FQuestlineGraphEditor::HighlightNodesInViewport(const TArray<UEdGraphNode*>& Nodes)
{
    if (GraphEditorWidget.IsValid())
    {
        GraphEditorWidget->SetHoverHighlightedNodes(Nodes);
    }
}

void FQuestlineGraphEditor::ClearNodeHighlight()
{
    if (GraphEditorWidget.IsValid())
    {
        GraphEditorWidget->ClearHoverHighlight();
    }
}

// Walks the editor graph hierarchy looking for the content node whose compiler-combined GUID matches ContentGuid.
// OuterGuidChain mirrors the compiler's CurrentOuterGuidChain — extended when descending into a LinkedQuestline's graph,
// preserved when descending into an inline Quest's inner graph.
static FQuestlineGraphEditor::FEdNodeLocation FindEdNodeInGraph(UEdGraph* Graph, const FGuid& ContentGuid, const FGuid& OuterGuidChain = FGuid())
{
    if (!Graph) return {};

    for (UEdGraphNode* Node : Graph->Nodes)
    {
        if (UQuestlineNode_ContentBase* Content = Cast<UQuestlineNode_ContentBase>(Node))
        {
            const FGuid Combined = FQuestlineGraphCompiler::CombineGuids(OuterGuidChain, Content->QuestGuid);
            if (Combined == ContentGuid)
                return { Graph, Node };
        }

        // Inline Quest: descend without extending the chain (compiler doesn't push for inline placements).
        if (UQuestlineNode_Quest* QuestNode = Cast<UQuestlineNode_Quest>(Node))
        {
            if (UEdGraph* InnerGraph = QuestNode->GetInnerGraph())
            {
                FQuestlineGraphEditor::FEdNodeLocation Inner = FindEdNodeInGraph(InnerGraph, ContentGuid, OuterGuidChain);
                if (Inner.IsValid()) return Inner;
            }
        }

        // LinkedQuestline: extend the chain with this wrapper's QuestGuid before descending into the linked asset's graph.
        if (UQuestlineNode_LinkedQuestline* LinkedNode = Cast<UQuestlineNode_LinkedQuestline>(Node))
        {
            if (!LinkedNode->LinkedGraph.IsNull())
            {
                if (UQuestlineGraph* LinkedAsset = LinkedNode->LinkedGraph.LoadSynchronous())
                {
                    const FGuid NewChain = FQuestlineGraphCompiler::CombineGuids(OuterGuidChain, LinkedNode->QuestGuid);
                    FQuestlineGraphEditor::FEdNodeLocation Linked = FindEdNodeInGraph(LinkedAsset->QuestlineEdGraph, ContentGuid, NewChain);
                    if (Linked.IsValid()) return Linked;
                }
            }
        }
    }

    return {};
}

TArray<FQuestlineBreadcrumb> FQuestlineGraphEditor::BuildBreadcrumbs(UEdGraph* Graph) const
{
    TArray<FQuestlineBreadcrumb> Crumbs;
    UEdGraph* Current = Graph;
    while (Current)
    {
        FQuestlineBreadcrumb Crumb;
        Crumb.Graph       = Current;
        Crumb.DisplayName = GetGraphDisplayName(Current);

        if (UQuestlineNode_Quest* QuestNode = Cast<UQuestlineNode_Quest>(Current->GetOuter()))
        {
            Crumb.EntryNode = QuestNode;
            Current = Cast<UEdGraph>(QuestNode->GetOuter());
        }
        else
        {
            Crumb.EntryNode = nullptr;
            Current = nullptr;
        }
        Crumbs.Insert(Crumb, 0);
    }
    return Crumbs;
}

FQuestlineGraphEditor::FEdNodeLocation FQuestlineGraphEditor::FindEdNodeLocation(const FGuid& ContentGuid) const
{
    return FindEdNodeInGraph(QuestlineGraph->QuestlineEdGraph, ContentGuid);
}

void FQuestlineGraphEditor::PinPrereqExaminer(UEdGraphNode* ContextNode)
{
    // Invoke the tab (creates it lazily if closed); SpawnPrereqExaminerTab caches the panel instance.
    TabManager->TryInvokeTab(PrereqExaminerTabId);
    if (PrereqExaminerPanel.IsValid())
    {
        PrereqExaminerPanel->PinContextNode(ContextNode);
    }
}

TSharedRef<SDockTab> FQuestlineGraphEditor::SpawnPrereqExaminerTab(const FSpawnTabArgs& Args)
{
    if (!PrereqExaminerPanel.IsValid())
    {
        PrereqExaminerPanel = SNew(SPrereqExaminerPanel);
    }
    return SNew(SDockTab)
        .Label(NSLOCTEXT("SimpleQuestEditor", "PrereqExaminerTabLabel", "Prereq Examiner"))
        [
            PrereqExaminerPanel.ToSharedRef()
        ];
}

void FQuestlineGraphEditor::OnOutlinerItemNavigate(TSharedPtr<FQuestlineOutlinerItem> Item)
{
    if (!Item.IsValid()) return;

    if (Item->ItemType == EOutlinerItemType::Root)
    {
        NavigateToEntry();
        return;
    }

    // Synthetic LinkedGraph intermediates (Pass 3 fallback for cases where the wrapper's own tag isn't in CompiledNodes)
    // have no Node, so there's nothing to look up by GUID. Fall back to opening the linked asset and jumping to its entry.
    // Real LinkedQuestline wrapper items classified by Pass 1 carry a Node and follow the unified path below.
    if (!Item->Node)
    {
        if (Item->ItemType != EOutlinerItemType::LinkedGraph || !Item->SourceGraph) return;
        UAssetEditorSubsystem* AssetEditors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
        if (!AssetEditors) return;
        AssetEditors->OpenEditorForAsset(Item->SourceGraph);
        IAssetEditorInstance* Instance = AssetEditors->FindEditorForAsset(Item->SourceGraph, false);
        FAssetEditorToolkit* Toolkit = static_cast<FAssetEditorToolkit*>(Instance);
        if (!Toolkit || Toolkit->GetToolkitFName() != TEXT("QuestlineGraphEditor")) return;
        FQuestlineGraphEditor* LinkedEditor = static_cast<FQuestlineGraphEditor*>(Toolkit);
        LinkedEditor->CrossAssetBackEditor = StaticCastSharedRef<FQuestlineGraphEditor>(AsShared());
        LinkedEditor->NavigateToEntry();
        return;
    }

    // Unified content-node navigation. FindEdNodeLocation walks this asset's graph hierarchy and descends into linked
    // questline asset graphs reachable from it, so it locates the EdNode regardless of which asset hosts it. Owning
    // asset is recovered from the EdNode's host UEdGraph via outer-chain walk, no per-item SourceGraph cache needed.
    FEdNodeLocation Location = FindEdNodeLocation(Item->Node->GetQuestGuid());
    if (!Location.IsValid()) return;

    UQuestlineGraph* HostAsset = nullptr;
    for (UObject* Outer = Location.HostGraph; Outer; Outer = Outer->GetOuter())
    {
        if (UQuestlineGraph* AsAsset = Cast<UQuestlineGraph>(Outer))
        {
            HostAsset = AsAsset;
            break;
        }
    }

    // Same-asset (or unresolvable host) - center locally. LinkedQuestline wrapper headers fall here too: their EdNode
    // lives in THIS asset's graph, so double-click centers on the wrapper instead of jumping into the linked asset.
    if (HostAsset == nullptr || HostAsset == QuestlineGraph)
    {
        NavigateToLocation(Location.HostGraph, Location.EdNode);
        return;
    }

    // Cross-asset: open the host asset's editor, hand off navigation to it.
    UAssetEditorSubsystem* AssetEditors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
    if (!AssetEditors) return;
    AssetEditors->OpenEditorForAsset(HostAsset);
    IAssetEditorInstance* Instance = AssetEditors->FindEditorForAsset(HostAsset, false);
    FAssetEditorToolkit* Toolkit = static_cast<FAssetEditorToolkit*>(Instance);
    if (!Toolkit || Toolkit->GetToolkitFName() != TEXT("QuestlineGraphEditor")) return;
    FQuestlineGraphEditor* LinkedEditor = static_cast<FQuestlineGraphEditor*>(Toolkit);
    LinkedEditor->CrossAssetBackEditor = StaticCastSharedRef<FQuestlineGraphEditor>(AsShared());
    LinkedEditor->NavigateToLocation(Location.HostGraph, Location.EdNode);
}

void FQuestlineGraphEditor::NavigateTo(UEdGraph* Graph)
{
    if (!bIsNavigatingHistory) GraphForwardStack.Empty();
    GraphBackwardStack.Add(Graph);
    GraphChangedHandles.Add(Graph->AddOnGraphChangedHandler(FOnGraphChanged::FDelegate::CreateSP(this, &FQuestlineGraphEditor::OnGraphChanged)));

    TSharedRef<SQuestlineGraphPanel> Panel = SNew(SQuestlineGraphPanel, Graph, GraphEditorCommands).GraphEvents(MakeGraphEvents());
    GraphEditorWidget = Panel;
    GraphPanelContainer->SetContent(Panel);

    // Rebuild breadcrumb trail from new graph hierarchy
    if (BreadcrumbBar.IsValid()) BreadcrumbBar->SetCrumbs(BuildBreadcrumbs(Graph));
}

void FQuestlineGraphEditor::NavigateToContentNode(const FGuid& ContentGuid)
{
    FEdNodeLocation Loc = FindEdNodeLocation(ContentGuid);
    if (Loc.IsValid())
        NavigateToLocation(Loc.HostGraph, Loc.EdNode);
}

void FQuestlineGraphEditor::NavigateToEntry()
{
    UEdGraph* CurrentGraph = GraphBackwardStack.IsEmpty() ? nullptr : GraphBackwardStack.Last();
    if (CurrentGraph != QuestlineGraph->QuestlineEdGraph)
        NavigateTo(QuestlineGraph->QuestlineEdGraph);

    if (!GraphEditorWidget.IsValid()) return;

    for (UEdGraphNode* Node : QuestlineGraph->QuestlineEdGraph->Nodes)
    {
        if (Cast<UQuestlineNode_Entry>(Node))
        {
            GraphEditorWidget->GetGraphEditor()->JumpToNode(Node, false, true);
            break;
        }
    }
}

void FQuestlineGraphEditor::NavigateToLocation(UEdGraph* HostGraph, UEdGraphNode* EdNode)
{
    if (!HostGraph || !EdNode) return;

    UEdGraph* CurrentGraph = GraphBackwardStack.IsEmpty() ? nullptr : GraphBackwardStack.Last();

    if (HostGraph != CurrentGraph)
    {
        if (HostGraph != QuestlineGraph->QuestlineEdGraph && CurrentGraph != QuestlineGraph->QuestlineEdGraph)
            NavigateTo(QuestlineGraph->QuestlineEdGraph);
        NavigateTo(HostGraph);
    }

    if (GraphEditorWidget.IsValid())
        GraphEditorWidget->JumpToNodeWhenReady(EdNode);
}

void FQuestlineGraphEditor::NavigateBack()
{
    if (GraphBackwardStack.Num() > 1)
    {
        UEdGraph* Leaving = GraphBackwardStack.Pop();
        Leaving->RemoveOnGraphChangedHandler(GraphChangedHandles.Pop());
        GraphForwardStack.Add(Leaving);                                                     // Save for forward navigation
        TGuardValue<bool> Guard(bIsNavigatingHistory, true);
        NavigateTo(GraphBackwardStack.Pop());                                         // re-navigate to previous (re-adds it)
    }
    else if (TSharedPtr<FQuestlineGraphEditor> Parent = CrossAssetBackEditor.Pin())
    {
        CrossAssetBackEditor.Reset();
        if (Parent.IsValid()) Parent->FocusWindow();
    }
}

void FQuestlineGraphEditor::NavigateForward()
{
    if (GraphForwardStack.Num() == 0) return;
    UEdGraph* Next = GraphForwardStack.Pop();
    TGuardValue Guard(bIsNavigatingHistory, true);
    NavigateTo(Next);
}

void FQuestlineGraphEditor::OnNodeDoubleClicked(UEdGraphNode* Node)
{
    if (UQuestlineNode_Quest* QuestNode = Cast<UQuestlineNode_Quest>(Node))
    {
        if (QuestNode->GetInnerGraph())
        {
            NavigateTo(QuestNode->GetInnerGraph());
        }
    }
}
