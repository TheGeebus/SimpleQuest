// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Toolkit/QuestlineGraphEditor.h"
#include "Quests/QuestlineGraph.h"
#include "GraphEditor.h"
#include "Widgets/Docking/SDockTab.h"
#include "Framework/Docking/TabManager.h"

const FName FQuestlineGraphEditor::GraphViewportTabId(TEXT("QuestlineGraphEditor_GraphViewport"));

void FQuestlineGraphEditor::InitQuestlineGraphEditor(
    const EToolkitMode::Type Mode,
    const TSharedPtr<IToolkitHost>& InitToolkitHost,
    UQuestlineGraph* InQuestlineGraph)
{
    QuestlineGraph = InQuestlineGraph;

    const TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout("QuestlineGraphEditor_Layout_v1")
        ->AddArea
        (
            FTabManager::NewPrimaryArea()
            ->SetOrientation(Orient_Horizontal)
            ->Split
            (
                FTabManager::NewStack()
                ->SetSizeCoefficient(1.f)
                ->AddTab(GraphViewportTabId, ETabState::OpenedTab)
            )
        );

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
}

void FQuestlineGraphEditor::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
    FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);
    InTabManager->UnregisterTabSpawner(GraphViewportTabId);
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

TSharedRef<SGraphEditor> FQuestlineGraphEditor::CreateGraphEditorWidget()
{
    check(QuestlineGraph);
    check(QuestlineGraph->QuestlineEdGraph);

    return SNew(SGraphEditor)
        .IsEditable(true)
        .GraphToEdit(QuestlineGraph->QuestlineEdGraph);
}
