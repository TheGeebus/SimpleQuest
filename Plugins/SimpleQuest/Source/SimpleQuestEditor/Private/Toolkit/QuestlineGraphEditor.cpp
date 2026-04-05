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
#include "Nodes/QuestlineNode_Quest.h"
#include "Quests/QuestNodeBase.h"
#include "Toolkit/QuestlineOutlinerPanel.h"
#include "Widgets/Navigation/SBreadcrumbTrail.h"


const FName FQuestlineGraphEditor::GraphViewportTabId(TEXT("QuestlineGraphEditor_GraphViewport"));
const FName FQuestlineGraphEditor::DetailsTabId(TEXT("QuestlineGraphEditor_Details"));
const FName FQuestlineGraphEditor::OutlinerTabId(TEXT("QuestlineGraphEditor_Outliner"));


FQuestlineGraphEditor::~FQuestlineGraphEditor()
{
    for (int32 i = 0; i < GraphBackwardStack.Num(); ++i)
    {
        if (GraphBackwardStack[i])
        {
            GraphBackwardStack[i]->RemoveOnGraphChangedHandler(GraphChangedHandles[i]);
        }
    }
}

void FQuestlineGraphEditor::InitQuestlineGraphEditor(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UQuestlineGraph* InQuestlineGraph)
{
    QuestlineGraph = InQuestlineGraph;

    const TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout("QuestlineGraphEditor_Layout_v4")
        ->AddArea
        (
            FTabManager::NewPrimaryArea()
            ->SetOrientation(Orient_Horizontal)
            ->Split
            (
                FTabManager::NewSplitter()
                ->SetOrientation(Orient_Vertical)
                ->SetSizeCoefficient(0.15f)
                ->Split
                (
                    FTabManager::NewStack()
                    ->SetSizeCoefficient(0.6f)
                    ->AddTab(OutlinerTabId, ETabState::OpenedTab)
                )
                ->Split
                (
                    FTabManager::NewStack()
                    ->SetSizeCoefficient(0.4f)
                    ->AddTab(DetailsTabId, ETabState::OpenedTab)
                )
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
    
    WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(
            NSLOCTEXT("SimpleQuestEditor", "WorkspaceMenu_QuestlineGraphEditor", "Questline Graph Editor"));

    InTabManager->RegisterTabSpawner(
        GraphViewportTabId,
        FOnSpawnTab::CreateSP(this, &FQuestlineGraphEditor::SpawnGraphViewportTab))
        .SetDisplayName(NSLOCTEXT("SimpleQuestEditor", "GraphViewportTab", "Questline Graph Editor"))
        .SetGroup(WorkspaceMenuCategory.ToSharedRef());

    InTabManager->RegisterTabSpawner(
        DetailsTabId,
        FOnSpawnTab::CreateSP(this, &FQuestlineGraphEditor::SpawnDetailsTab))
        .SetDisplayName(NSLOCTEXT("SimpleQuestEditor", "DetailsTab", "Details"))
        .SetGroup(WorkspaceMenuCategory.ToSharedRef());

    InTabManager->RegisterTabSpawner(
        OutlinerTabId,
        FOnSpawnTab::CreateSP(this, &FQuestlineGraphEditor::SpawnOutlinerTab))
        .SetDisplayName(NSLOCTEXT("SimpleQuestEditor", "OutlinerTab", "Questline Outliner"))
        .SetGroup(WorkspaceMenuCategory.ToSharedRef());
}

void FQuestlineGraphEditor::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
    InTabManager->UnregisterTabSpawner(GraphViewportTabId);
    InTabManager->UnregisterTabSpawner(DetailsTabId);
    InTabManager->UnregisterTabSpawner(OutlinerTabId);
    FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);
}

TSharedRef<SDockTab> FQuestlineGraphEditor::SpawnGraphViewportTab(const FSpawnTabArgs& Args)
{
    GraphPanelContainer = SNew(SBox);

    SAssignNew(BreadcrumbTrail, SBreadcrumbTrail<UEdGraph*>)
        .ButtonStyle(FAppStyle::Get(), "GraphBreadcrumbButton")
        .TextStyle(FAppStyle::Get(), "GraphBreadcrumbButtonText")
        .DelimiterImage(FAppStyle::GetBrush("BreadcrumbTrail.Delimiter"))
        .PersistentBreadcrumbs(true)
        .OnCrumbClicked(this, &FQuestlineGraphEditor::OnBreadcrumbClicked);

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
                        BreadcrumbTrail.ToSharedRef()
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
            Node->Modify();
            Node->GetSchema()->BreakNodeLinks(*Node);
            CurrentGraph->RemoveNode(Node);
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
    ToolbarBuilder.AddToolBarButton(FQuestlineGraphEditorCommands::Get().CompileQuestlineGraph, NAME_None, TAttribute<FText>(), TAttribute<FText>(),
        TAttribute<FSlateIcon>::CreateLambda(
            [this]() -> FSlateIcon
            {
                return GetCompileStatusIcon();
            })
        );
    ToolbarBuilder.EndSection();
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

FQuestlineGraphEditor::FEdNodeLocation FQuestlineGraphEditor::FindEdNodeLocation(const FGuid& ContentGuid) const
{
    if (!QuestlineGraph || !QuestlineGraph->QuestlineEdGraph) return {};

    for (UEdGraphNode* Node : QuestlineGraph->QuestlineEdGraph->Nodes)
    {
        UQuestlineNode_ContentBase* Content = Cast<UQuestlineNode_ContentBase>(Node);
        if (!Content) continue;

        if (Content->QuestGuid == ContentGuid)
            return { QuestlineGraph->QuestlineEdGraph, Node };

        if (UQuestlineNode_Quest* QuestNode = Cast<UQuestlineNode_Quest>(Content))
        {
            if (UEdGraph* Inner = QuestNode->GetInnerGraph())
            {
                for (UEdGraphNode* InnerNode : Inner->Nodes)
                {
                    UQuestlineNode_ContentBase* InnerContent = Cast<UQuestlineNode_ContentBase>(InnerNode);
                    if (InnerContent && InnerContent->QuestGuid == ContentGuid)
                        return { Inner, InnerNode };
                }
            }
        }
    }
    return {};
}

void FQuestlineGraphEditor::OnOutlinerItemNavigate(TSharedPtr<FQuestlineOutlinerItem> Item)
{
    if (!Item.IsValid()) return;

    // Local item — navigate within this editor
    if (Item->LinkDepth == 0)
    {
        if (Item->ItemType == EOutlinerItemType::Root) return;  // nothing to navigate to
        NavigateToContentNode(Item->Node->GetQuestGuid());
        return;
    }

    // Linked item — open or focus the source asset's editor
    UQuestlineGraph* SourceAsset = Item->SourceGraph;
    if (!SourceAsset) return;

    UAssetEditorSubsystem* AssetEditors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
    AssetEditors->OpenEditorForAsset(SourceAsset);

    IAssetEditorInstance* Instance = AssetEditors->FindEditorForAsset(SourceAsset, false);
    FAssetEditorToolkit* Toolkit = static_cast<FAssetEditorToolkit*>(Instance);
    if (!Toolkit || Toolkit->GetToolkitFName() != TEXT("QuestlineGraphEditor")) return;

    FQuestlineGraphEditor* LinkedEditor = static_cast<FQuestlineGraphEditor*>(Toolkit);

    if (Item->ItemType == EOutlinerItemType::LinkedGraph)
    {
        LinkedEditor->NavigateToEntry();
    }
    else
    {
        LinkedEditor->NavigateToContentNode(Item->Node->GetQuestGuid());
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

    // Rebuild breadcrumb trail from full navigation stack
    if (BreadcrumbTrail.IsValid())
    {
        BreadcrumbTrail->ClearCrumbs();
        for (UEdGraph* StackGraph : GraphBackwardStack)
        {
            BreadcrumbTrail->PushCrumb(GetGraphDisplayName(StackGraph), StackGraph);
        }
    }
}

void FQuestlineGraphEditor::NavigateToContentNode(const FGuid& ContentGuid)
{
    FEdNodeLocation Location = FindEdNodeLocation(ContentGuid);
    if (!Location.IsValid()) return;

    UEdGraph* CurrentGraph = GraphBackwardStack.IsEmpty() ? nullptr : GraphBackwardStack.Last();

    if (Location.HostGraph != CurrentGraph)
    {
        // If the target is an inner graph and we're not already at root, go to root first
        if (Location.HostGraph != QuestlineGraph->QuestlineEdGraph && CurrentGraph != QuestlineGraph->QuestlineEdGraph)
        {
            NavigateTo(QuestlineGraph->QuestlineEdGraph);
        }
        NavigateTo(Location.HostGraph);
    }

    if (GraphEditorWidget.IsValid())
    {
        GraphEditorWidget->GetGraphEditor()->JumpToNode(Location.EdNode, false, true);
    }
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

void FQuestlineGraphEditor::NavigateBack()
{
    if (GraphBackwardStack.Num() <= 1) return;
    UEdGraph* Leaving = GraphBackwardStack.Pop();
    Leaving->RemoveOnGraphChangedHandler(GraphChangedHandles.Pop());
    GraphForwardStack.Add(Leaving);                                                     // Save for forward navigation
    TGuardValue<bool> Guard(bIsNavigatingHistory, true);
    NavigateTo(GraphBackwardStack.Pop());                                         // re-navigate to previous (re-adds it)
}

void FQuestlineGraphEditor::NavigateForward()
{
    if (GraphForwardStack.Num() == 0) return;
    UEdGraph* Next = GraphForwardStack.Pop();
    TGuardValue Guard(bIsNavigatingHistory, true);
    NavigateTo(Next);
}

void FQuestlineGraphEditor::OnBreadcrumbClicked(UEdGraph* const& Graph)
{
    // Pop everything above the clicked graph
    while (GraphBackwardStack.Num() > 0 && GraphBackwardStack.Last() != Graph)
    {
        UEdGraph* Leaving = GraphBackwardStack.Pop();
        Leaving->RemoveOnGraphChangedHandler(GraphChangedHandles.Pop());
        GraphForwardStack.Add(Leaving);
    }
    if (GraphBackwardStack.Num() > 0)
    {
        UEdGraph* Target = GraphBackwardStack.Pop();
        Target->RemoveOnGraphChangedHandler(GraphChangedHandles.Pop());
        TGuardValue Guard(bIsNavigatingHistory, true);
        NavigateTo(Target);  // re-adds it cleanly
    }
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
