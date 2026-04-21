// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Toolkit/QuestlineGraphEditor.h"
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
#include "Modules/ModuleManager.h"
#include "Nodes/QuestlineNode_Entry.h"
#include "Nodes/QuestlineNode_LinkedQuestline.h"
#include "Nodes/QuestlineNode_Quest.h"
#include "Quests/QuestNodeBase.h"
#include "Toolkit/QuestlineOutlinerPanel.h"
#include "Utilities/SimpleQuestEditorUtils.h"
#include "Widgets/SGroupExaminerPanel.h"
#include "Widgets/SPrereqExaminerPanel.h"



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
        FExecuteAction::CreateSP(this, &FQuestlineGraphEditor::DeleteSelectedNodes));
    
    GraphEditorCommands->MapAction(
       FQuestlineGraphEditorCommands::Get().CompileQuestlineGraph,
       FExecuteAction::CreateSP(this, &FQuestlineGraphEditor::CompileQuestlineGraph));

    GraphEditorCommands->MapAction(
        FQuestlineGraphEditorCommands::Get().CompileAllQuestlineGraphs,
        FExecuteAction::CreateLambda([]()
        {
            ISimpleQuestEditorModule::Get().CompileAllQuestlineGraphs();
        }));
    
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

void FQuestlineGraphEditor::CompileQuestlineGraph()
{
    TUniquePtr<FQuestlineGraphCompiler> Compiler = ISimpleQuestEditorModule::Get().CreateCompiler();
    const bool bSuccess = Compiler->Compile(QuestlineGraph);

    // Apply detected tag renames to loaded worlds
    int32 RenamedActors = 0;
    const TMap<FName, FName>& DetectedRenames = Compiler->GetDetectedRenames();
    if (bSuccess && DetectedRenames.Num() > 0)
    {
        RenamedActors = FSimpleQuestEditorUtilities::ApplyTagRenamesToLoadedWorlds(DetectedRenames);
    }
    
    // Flush compiler messages to the Quest Compiler MessageLog panel
    if (Compiler->GetMessages().Num() > 0)
    {
        FMessageLog CompilerLog("QuestCompiler");
        CompilerLog.NewPage(FText::Format(NSLOCTEXT("SimpleQuestEditor", "CompilePageLabel", "{0}"), FText::FromString(QuestlineGraph->GetName())));
        CompilerLog.AddMessages(Compiler->GetMessages());

        if (Compiler->GetNumErrors() > 0)
        {
            CompilerLog.Notify(FText::Format(NSLOCTEXT("SimpleQuestEditor", "CompileErrors", "Quest compilation: {0} error(s)"), Compiler->GetNumErrors()));
        }
        else if (Compiler->GetNumWarnings() > 0)
        {
            CompilerLog.Notify(FText::Format(NSLOCTEXT("SimpleQuestEditor", "CompileWarnings", "Quest compilation: {0} warning(s)"), Compiler->GetNumWarnings()));
        }
    }
    else
    {
        // Clean compile — simple success toast
        FNotificationInfo Info(NSLOCTEXT("SimpleQuestEditor", "CompileSuccess", "Questline compiled successfully."));
        Info.ExpireDuration = 3.f;
        Info.bUseSuccessFailIcons = true;
        FSlateNotificationManager::Get().AddNotification(Info)->SetCompletionState(SNotificationItem::CS_Success);
    }

    // Tag rename toast message
    if (DetectedRenames.Num() > 0)
    {
        FNotificationInfo RenameInfo(FText::Format(
            NSLOCTEXT("SimpleQuestEditor", "TagRenames",
                "{0} tag(s) renamed. {1} actor(s) updated in loaded levels."),
            DetectedRenames.Num(), RenamedActors));
        RenameInfo.ExpireDuration = 5.f;
        RenameInfo.bUseSuccessFailIcons = true;
        FSlateNotificationManager::Get().AddNotification(RenameInfo)->SetCompletionState(SNotificationItem::CS_Success);
    }

    // Rebuild node widgets — live queries (watching actors) depend on compiled tags. Must set CompileStatus AFTER this:
    // NotifyGraphChanged fires OnGraphChanged which resets status to Unknown.
    if (bSuccess) RefreshAllNodeWidgets();

    CompileStatus = bSuccess ? EQuestlineCompileStatus::UpToDate : EQuestlineCompileStatus::Error;
    if (bSuccess && OutlinerPanel.IsValid()) OutlinerPanel->Refresh();
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
    if (QuestlineGraph->GetOutermost()->GetName() != PackagePath) return;

    if (bSuccess) RefreshAllNodeWidgets();

    CompileStatus = bSuccess ? EQuestlineCompileStatus::UpToDate : EQuestlineCompileStatus::Error;
    if (bSuccess && OutlinerPanel.IsValid()) OutlinerPanel->Refresh();

}

void FQuestlineGraphEditor::RefreshAllNodeWidgets()
{
    if (!QuestlineGraph || !QuestlineGraph->QuestlineEdGraph) return;

    QuestlineGraph->QuestlineEdGraph->NotifyGraphChanged();

    for (UEdGraphNode* Node : QuestlineGraph->QuestlineEdGraph->Nodes)
    {
        if (UQuestlineNode_Quest* QuestNode = Cast<UQuestlineNode_Quest>(Node))
        {
            if (UEdGraph* InnerGraph = QuestNode->GetInnerGraph())
            {
                InnerGraph->NotifyGraphChanged();
            }
        }
    }
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
