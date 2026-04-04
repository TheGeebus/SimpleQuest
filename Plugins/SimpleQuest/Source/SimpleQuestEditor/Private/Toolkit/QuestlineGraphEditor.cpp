// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Toolkit/QuestlineGraphEditor.h"
#include "Toolkit/QuestlineGraphPanel.h"
#include "Quests/QuestlineGraph.h"
#include "GraphEditor.h"
#include "GraphEditorActions.h"
#include "Utilities/QuestlineGraphCompiler.h"
#include "Widgets/Docking/SDockTab.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/Commands/GenericCommands.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Toolkit/QuestlineGraphEditorCommands.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "PropertyEditorModule.h"
#include "Modules/ModuleManager.h"


const FName FQuestlineGraphEditor::GraphViewportTabId(TEXT("QuestlineGraphEditor_GraphViewport"));
const FName FQuestlineGraphEditor::DetailsTabId(TEXT("QuestlineGraphEditor_Details"));


FQuestlineGraphEditor::~FQuestlineGraphEditor() = default;

void FQuestlineGraphEditor::InitQuestlineGraphEditor(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UQuestlineGraph* InQuestlineGraph)
{
    QuestlineGraph = InQuestlineGraph;

    const TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout("QuestlineGraphEditor_Layout_v3")
        ->AddArea
        (
            FTabManager::NewPrimaryArea()
            ->SetOrientation(Orient_Horizontal)
            ->Split
            (
                FTabManager::NewStack()
                ->SetSizeCoefficient(0.15f)
                ->AddTab(DetailsTabId, ETabState::OpenedTab)
            )
            ->Split
            (
                FTabManager::NewStack()
                ->SetSizeCoefficient(0.85f)
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

    InTabManager->RegisterTabSpawner(
        GraphViewportTabId,
        FOnSpawnTab::CreateSP(this, &FQuestlineGraphEditor::SpawnGraphViewportTab))
        .SetDisplayName(NSLOCTEXT("SimpleQuestEditor", "GraphViewportTab", "Viewport"));

    InTabManager->RegisterTabSpawner(
        DetailsTabId,
        FOnSpawnTab::CreateSP(this, &FQuestlineGraphEditor::SpawnDetailsTab))
        .SetDisplayName(NSLOCTEXT("SimpleQuestEditor", "DetailsTab", "Details"));
}

void FQuestlineGraphEditor::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
    FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);
    InTabManager->UnregisterTabSpawner(GraphViewportTabId);
    InTabManager->UnregisterTabSpawner(DetailsTabId);
}

TSharedRef<SDockTab> FQuestlineGraphEditor::SpawnGraphViewportTab(const FSpawnTabArgs& Args)
{
    GraphEditorWidget = CreateGraphEditorWidget();

    return SNew(SDockTab)
        .Label(NSLOCTEXT("SimpleQuestEditor", "GraphViewportTabLabel", "Graph"))
        [
            GraphEditorWidget.ToSharedRef()
        ];
}

TSharedRef<SQuestlineGraphPanel> FQuestlineGraphEditor::CreateGraphEditorWidget()
{
    check(QuestlineGraph);
    check(QuestlineGraph->QuestlineEdGraph);

    SGraphEditor::FGraphEditorEvents GraphEvents;
    GraphEvents.OnSelectionChanged = SGraphEditor::FOnSelectionChanged::CreateSP(
        this, &FQuestlineGraphEditor::OnGraphSelectionChanged);

    return SNew(SQuestlineGraphPanel, QuestlineGraph->QuestlineEdGraph, GraphEditorCommands)
        .GraphEvents(GraphEvents);
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
}

void FQuestlineGraphEditor::DeleteSelectedNodes()
{
    if (!GraphEditorWidget.IsValid()) return;

    const FScopedTransaction Transaction(
        NSLOCTEXT("SimpleQuestEditor", "DeleteSelectedNodes", "Delete Selected Nodes"));

    QuestlineGraph->QuestlineEdGraph->Modify();

    for (UObject* Obj : GraphEditorWidget->GetGraphEditor()->GetSelectedNodes())
    {
        UEdGraphNode* Node = Cast<UEdGraphNode>(Obj);
        if (Node && Node->CanUserDeleteNode())
        {
            Node->Modify();
            Node->GetSchema()->BreakNodeLinks(*Node);
            QuestlineGraph->QuestlineEdGraph->RemoveNode(Node);
        }
    }
}

void FQuestlineGraphEditor::CompileQuestlineGraph()
{
    TUniquePtr<FQuestlineGraphCompiler> Compiler = ISimpleQuestEditorModule::Get().CreateCompiler();
    const bool bSuccess = Compiler->Compile(QuestlineGraph);

    const FText Notification = bSuccess
        ? NSLOCTEXT("SimpleQuestEditor", "CompileSuccess", "Questline compiled successfully.")
        : NSLOCTEXT("SimpleQuestEditor", "CompileFailed", "Questline compilation failed. Check the Output Log for details.");

    FNotificationInfo Info(Notification);
    Info.ExpireDuration = 3.f;
    Info.bUseSuccessFailIcons = true;
    FSlateNotificationManager::Get().AddNotification(Info)->SetCompletionState(bSuccess ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
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
    ToolbarBuilder.AddToolBarButton(FQuestlineGraphEditorCommands::Get().CompileQuestlineGraph);
    ToolbarBuilder.EndSection();
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

    TArray<UObject*> Selected;
    for (UObject* Obj : SelectedNodes)
        Selected.Add(Obj);

    DetailsView->SetObjects(Selected);
}

