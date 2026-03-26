#include "SimpleQuestEditorModule.h"
#include "AssetTypes/QuestlineGraphAssetTypeActions.h"
#include "AssetToolsModule.h"
#include "Modules/ModuleManager.h"

IMPLEMENT_MODULE(FSimpleQuestEditor, SimpleQuestEditor);

void FSimpleQuestEditor::StartupModule()
{
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	QuestlineGraphAssetTypeActions = MakeShared<FQuestlineGraphAssetTypeActions>();
	AssetTools.RegisterAssetTypeActions(QuestlineGraphAssetTypeActions.ToSharedRef());
}

void FSimpleQuestEditor::ShutdownModule()
{
	if (FModuleManager::Get().IsModuleLoaded("AssetTools"))
	{
		IAssetTools& AssetTools = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools").Get();
		AssetTools.UnregisterAssetTypeActions(QuestlineGraphAssetTypeActions.ToSharedRef());
	}
	QuestlineGraphAssetTypeActions.Reset();
}
