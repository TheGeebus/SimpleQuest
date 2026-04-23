// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Toolkit/QuestlineGraphEditor.h"

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


const FName FQuestlineGraphEditor::GraphViewportTabId(TEXT("QuestlineGraphEditor_GraphViewport"));
const FName FQuestlineGraphEditor::DetailsTabId(TEXT("QuestlineGraphEditor_Details"));
const FName FQuestlineGraphEditor::OutlinerTabId(TEXT("QuestlineGraphEditor_Outliner"));
const FName FQuestlineGraphEditor::GroupExaminerTabId(TEXT("QuestlineGraphEditor_GroupExaminer"));
const FName FQuestlineGraphEditor::PrereqExaminerTabId(TEXT("QuestlineGraphEditor_PrereqExaminer"));


FQuestlineGraphEditor::~FQuestlineGraphEditor()
{
    ISimpleQuestEditorModule::Get().OnQuestlineCompiled().Remove(ExternalCompileHandle);

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

                IAssetEditorInstance* Instance = EditorSubsystem->FindEditorForAsset(AssetGraph, /*bFocusIfOpen=*/ false);
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
    ExternalCompileHandle = ISimpleQuestEditorModule::Get().OnQuestlineCompiled().AddSP(this, &FQuestlineGraphEditor::OnExternalCompile);
    
    const TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout("QuestlineGraphEditor_Layout_v10")
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
                FTabManager::NewStack()
                ->SetSizeCoefficient(0.80f)
                ->AddTab(GraphViewportTabId, ETabState::OpenedTab)
            )
        );
    
    BindGraphCommands();
    ExtendToolbar();

    FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
    FDetailsViewArgs DetailsViewArgs;
    DetailsViewArgs.bHideSelectionTip = true;
    DetailsViewArgs.bAllowSearch = false;
    DetailsView = PropertyModule.CreateDetailView(DetailsViewArgs);

    // Initial target — show the asset itself (QuestlineID, FriendlyName, etc.) until the designer selects a node.
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
        .SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "BlueprintEditor.FindInBlueprint")); /* "Kismet.Tabs.FindResults" "Kismet.FindInBlueprints.MenuIcon" "BlueprintEditor.FindInBlueprint" */
}

void FQuestlineGraphEditor::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
    InTabManager->UnregisterTabSpawner(GraphViewportTabId);
    InTabManager->UnregisterTabSpawner(DetailsTabId);
    InTabManager->UnregisterTabSpawner(OutlinerTabId);
    InTabManager->UnregisterTabSpawner(GroupExaminerTabId);
    InTabManager->UnregisterTabSpawner(PrereqExaminerTabId);
    FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);
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
       FQuestlineGraphEditorCommands::Get().CompileQuestlineGraph,
       FExecuteAction::CreateSP(this, &FQuestlineGraphEditor::CompileQuestlineGraph));

    GraphEditorCommands->MapAction(
        FQuestlineGraphEditorCommands::Get().CompileAllQuestlineGraphs,
        FExecuteAction::CreateLambda([]()
        {
            ISimpleQuestEditorModule::Get().CompileAllQuestlineGraphs();
        }));
        
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
            //Node->Modify();
            //Node->GetSchema()->BreakNodeLinks(*Node);
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

void FQuestlineGraphEditor::CompileQuestlineGraph()
{
    // Aggregate state across primary + linked neighborhood.
    int32 TotalErrors = 0;
    int32 TotalWarnings = 0;
    int32 TotalRenames = 0;
    int32 TotalRenamedActors = 0;
    int32 NeighborSuccessCount = 0;
    int32 NeighborFailCount = 0;

    FMessageLog CompilerLog("QuestCompiler");
    bool bLogPageOpen = false;
    auto EnsurePage = [&]()
    {
        if (bLogPageOpen) return;
        CompilerLog.NewPage(FText::Format(NSLOCTEXT("SimpleQuestEditor", "CompilePageLabel", "{0}"),
            FText::FromString(QuestlineGraph->GetName())));
        bLogPageOpen = true;
    };

    // Per-asset compile body — runs for primary and each neighbor. Broadcasts OnQuestlineCompiled per asset so
    // every open editor (this one + any others) refreshes via the existing OnExternalCompile path.
    auto CompileAsset = [&](UQuestlineGraph* Graph, bool bIsPrimary)
    {
        if (!Graph) return;

        TUniquePtr<FQuestlineGraphCompiler> Compiler = ISimpleQuestEditorModule::Get().CreateCompiler();
        const bool bSuccess = Compiler->Compile(Graph);

        if (bSuccess)
        {
            if (!bIsPrimary) ++NeighborSuccessCount;
            const TMap<FName, FName>& Renames = Compiler->GetDetectedRenames();
            if (Renames.Num() > 0)
            {
                TotalRenames       += Renames.Num();
                TotalRenamedActors += FSimpleQuestEditorUtilities::ApplyTagRenamesToLoadedWorlds(Renames);
            }
        }
        else if (!bIsPrimary)
        {
            ++NeighborFailCount;
        }
        TotalErrors   += Compiler->GetNumErrors();
        TotalWarnings += Compiler->GetNumWarnings();

        if (Compiler->GetMessages().Num() > 0)
        {
            EnsurePage();
            CompilerLog.AddMessages(Compiler->GetMessages());
        }

        ISimpleQuestEditorModule::Get().OnQuestlineCompiled().Broadcast(
            Graph->GetOutermost()->GetName(), bSuccess);
    };

    // Primary first so its status/outliner update (via OnExternalCompile's bIsOwnAsset branch) reflects its own result.
    CompileAsset(QuestlineGraph, /*bIsPrimary=*/ true);

    // Linked neighborhood — bidirectional transitive closure of LinkedQuestline references. See
    // ISimpleQuestEditorModule::CollectLinkedNeighborhood.
    TArray<UQuestlineGraph*> Neighborhood;
    ISimpleQuestEditorModule::Get().CollectLinkedNeighborhood(QuestlineGraph, Neighborhood);

    if (Neighborhood.Num() > 0)
    {
        UE_LOG(LogSimpleQuest, Log, TEXT("Compile: auto-compiling %d linked neighbor(s) of '%s'"), Neighborhood.Num(), *QuestlineGraph->GetName());
    }

    for (UQuestlineGraph* Neighbor : Neighborhood)
    {
        CompileAsset(Neighbor, /*bIsPrimary=*/ false);
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

    if (TotalRenames > 0)
    {
        FNotificationInfo RenameInfo(FText::Format(
            NSLOCTEXT("SimpleQuestEditor", "TagRenames",
                "{0} tag(s) renamed. {1} actor(s) updated in loaded levels."),
            TotalRenames, TotalRenamedActors));
        RenameInfo.ExpireDuration = 5.f;
        RenameInfo.bUseSuccessFailIcons = true;
        FSlateNotificationManager::Get().AddNotification(RenameInfo)->SetCompletionState(SNotificationItem::CS_Success);
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
        /*bInSimpleComboBox=*/ true);

    // Compile All — dedicated button
    ToolbarBuilder.AddToolBarButton(
        FQuestlineGraphEditorCommands::Get().CompileAllQuestlineGraphs,
        NAME_None,
        NSLOCTEXT("SimpleQuestEditor", "CompileAll_Label", "All"),
        NSLOCTEXT("SimpleQuestEditor", "CompileAll_Tooltip", "Compile and save every questline graph in the project"),
        FSlateIcon(FAppStyle::GetAppStyleSetName(), "Blueprint.CompileStatus.Background"));

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
}

TSharedRef<SWidget> FQuestlineGraphEditor::GenerateCompileOptionsMenu()
{
    FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection=*/ true, GraphEditorCommands);

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

    // Empty selection — restore the asset view so graph-level metadata (QuestlineID, FriendlyName) stays reachable
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

static FQuestlineGraphEditor::FEdNodeLocation FindEdNodeInGraph(UEdGraph* Graph, const FGuid& ContentGuid)
{
    if (!Graph) return {};

    for (UEdGraphNode* Node : Graph->Nodes)
    {
        // GUID match — this node is the target
        if (UQuestlineNode_ContentBase* Content = Cast<UQuestlineNode_ContentBase>(Node))
        {
            if (Content->QuestGuid == ContentGuid)
                return { Graph, Node };
        }

        // Recurse into quest inner graphs
        if (UQuestlineNode_Quest* QuestNode = Cast<UQuestlineNode_Quest>(Node))
        {
            if (UEdGraph* InnerGraph = QuestNode->GetInnerGraph())
            {
                FQuestlineGraphEditor::FEdNodeLocation Inner = FindEdNodeInGraph(InnerGraph, ContentGuid);
                if (Inner.IsValid()) return Inner;
            }
        }

        // Recurse into linked questline graphs
        if (UQuestlineNode_LinkedQuestline* LinkedNode = Cast<UQuestlineNode_LinkedQuestline>(Node))
        {
            if (!LinkedNode->LinkedGraph.IsNull())
            {
                if (UQuestlineGraph* LinkedAsset = LinkedNode->LinkedGraph.LoadSynchronous())
                {
                    FQuestlineGraphEditor::FEdNodeLocation Linked = FindEdNodeInGraph(LinkedAsset->QuestlineEdGraph, ContentGuid);
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
    
    if (Item->LinkDepth == 0)
    {
        NavigateToContentNode(Item->Node->GetQuestGuid());
        return;
    }

    UQuestlineGraph* SourceAsset = Item->SourceGraph;
    if (!SourceAsset) return;

    UAssetEditorSubsystem* AssetEditors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
    AssetEditors->OpenEditorForAsset(SourceAsset);

    IAssetEditorInstance* Instance = AssetEditors->FindEditorForAsset(SourceAsset, false);
    FAssetEditorToolkit* Toolkit = static_cast<FAssetEditorToolkit*>(Instance);
    if (!Toolkit || Toolkit->GetToolkitFName() != TEXT("QuestlineGraphEditor")) return;

    FQuestlineGraphEditor* LinkedEditor = static_cast<FQuestlineGraphEditor*>(Toolkit);
    LinkedEditor->CrossAssetBackEditor = StaticCastSharedRef<FQuestlineGraphEditor>(AsShared());

    if (Item->ItemType == EOutlinerItemType::LinkedGraph)
    {
        LinkedEditor->NavigateToEntry();
    }
    else
    {
        FEdNodeLocation Location = FindEdNodeLocation(Item->Node->GetQuestGuid()); 
        if (!Location.IsValid()) return;
        LinkedEditor->NavigateToLocation(Location.HostGraph, Location.EdNode);
    }
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
