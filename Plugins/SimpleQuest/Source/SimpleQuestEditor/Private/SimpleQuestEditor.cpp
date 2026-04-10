// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "SimpleQuestEditor.h"
#include "AssetTypes/QuestlineGraphAssetTypeActions.h"
#include "AssetToolsModule.h"
#include "Modules/ModuleManager.h"
#include "EdGraphUtilities.h"
#include "GameplayTagsManager.h"
#include "Utilities/QuestlineGraphCompiler.h"
#include "SGraphNodeKnot.h"
#include "SimpleQuestLog.h"
#include "Graph/QuestlineGraphSchema.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Nodes/QuestlineNode_Knot.h"
#include "Settings/SimpleQuestSettings.h"
#include "Toolkit/QuestlineGraphEditorCommands.h"
#include "Utilities/QuestStateTagUtils.h"
#include "Engine/Blueprint.h"
#include "Quests/QuestlineGraph.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Utilities/SimpleCoreDebug.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"


IMPLEMENT_MODULE(FSimpleQuestEditor, SimpleQuestEditor);

class FQuestlineGraphNodeFactory : public FGraphPanelNodeFactory
{
	virtual TSharedPtr<SGraphNode> CreateNode(UEdGraphNode* Node) const override
	{
		if (UQuestlineNode_Knot* KnotNode = Cast<UQuestlineNode_Knot>(Node))
		{
			return SNew(SGraphNodeKnot, KnotNode);
		}
		return nullptr;
	}
};

void FSimpleQuestEditor::StartupModule()
{
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	QuestlineGraphAssetTypeActions = MakeShared<FQuestlineGraphAssetTypeActions>();
	AssetTools.RegisterAssetTypeActions(QuestlineGraphAssetTypeActions.ToSharedRef());

	QuestlineGraphNodeFactory = MakeShared<FQuestlineGraphNodeFactory>();
	FEdGraphUtilities::RegisterVisualNodeFactory(QuestlineGraphNodeFactory);

	QuestlineConnectionFactory = UQuestlineGraphSchema::MakeQuestlineConnectionFactory();
	FEdGraphUtilities::RegisterVisualPinConnectionFactory(QuestlineConnectionFactory);

	FQuestlineGraphEditorCommands::Register();

	FAssetRegistryModule& ARModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AR = ARModule.Get();
	// Always subscribe — OnFilesLoaded fires when async loading finishes, whenever that is.
	// If the AR is already done loading when we subscribe, the delegate will not fire, so
	// we also call immediately as a fallback for that case.
	AR.OnFilesLoaded().AddRaw(this, &FSimpleQuestEditor::RegisterTagsFromAssetRegistry);
	AR.OnAssetRemoved().AddRaw(this, &FSimpleQuestEditor::OnAssetRemoved);
	if (!AR.IsLoadingAssets())
	{
		RegisterTagsFromAssetRegistry();
	}

	// Blueprint compile check requires a fully initialized editor — keep in delegate.
	FEditorDelegates::OnEditorInitialized.AddLambda([this](double)
	{
		const USimpleQuestSettings* Settings = GetDefault<USimpleQuestSettings>();
		FString ClassPath = Settings->QuestManagerClass.ToSoftObjectPath().ToString();
		if (!ClassPath.RemoveFromEnd(TEXT("_C")))
		{
			return;
		}
		UObject* Loaded = StaticLoadObject(UObject::StaticClass(), nullptr, *ClassPath, nullptr, LOAD_NoWarn);
		if (UBlueprint* BP = Cast<UBlueprint>(Loaded))
		{
			if (BP->Status != EBlueprintStatus::BS_UpToDate)
			{
				FKismetEditorUtilities::CompileBlueprint(BP, EBlueprintCompileOptions::SkipGarbageCollection);
				BP->MarkPackageDirty();
			}
		}
	});
}

void FSimpleQuestEditor::RegisterTagsFromAssetRegistry()
{
	if (bIsRegisteringTags) return;
	TGuardValue<bool> Guard(bIsRegisteringTags, true);

	FAssetRegistryModule* ARModule = FModuleManager::GetModulePtr<FAssetRegistryModule>("AssetRegistry");
	if (!ARModule) return;

	IAssetRegistry& AR = ARModule->Get();

	FARFilter Filter;
	Filter.ClassPaths.Add(UQuestlineGraph::StaticClass()->GetClassPathName());
	Filter.bRecursiveClasses = true;

	TArray<FAssetData> QuestlineAssets;
	AR.GetAssets(Filter, QuestlineAssets);

	UE_LOG(LogSimpleQuest, Display, TEXT("SimpleQuestEditor: RegisterTagsFromAssetRegistry — found %d questline asset(s)"), QuestlineAssets.Num());

	for (const FAssetData& AssetData : QuestlineAssets)
	{
		FAssetTagValueRef TagValue = AssetData.TagsAndValues.FindTag(TEXT("CompiledQuestTags"));
		if (!TagValue.IsSet() || TagValue.GetValue().IsEmpty())
		{
			UE_LOG(LogSimpleQuest, Display, TEXT("  %s — no CompiledQuestTags metadata (not yet compiled+saved)"), *AssetData.AssetName.ToString());
			continue;
		}

		TArray<FString> TagStrings;
		TagValue.GetValue().ParseIntoArray(TagStrings, TEXT("|"), true);

		TArray<FName> TagNames;
		TagNames.Reserve(TagStrings.Num());
		for (const FString& TagStr : TagStrings)
		{
			TagNames.Add(FName(*TagStr));
		}

		CompiledTagRegistry.Add(AssetData.GetObjectPathString(), MoveTemp(TagNames));
		UE_LOG(LogSimpleQuest, Display, TEXT("  %s — loaded %d tag(s)"), *AssetData.AssetName.ToString(), TagStrings.Num());
	}
	
	// The INI is the authoritative compiled state. Only generate it from AR metadata when it doesn't already exist (first
	// run, deleted file). After that it is exclusively maintained by RegisterCompiledTags at compile time. Writing it unconditionally
	// here would overwrite a correctly-compiled INI with stale AR metadata whenever a graph was compiled but not saved.
	const FString IniPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectConfigDir() / TEXT("Tags/SimpleQuestCompiledTags.ini"));
	if (!FPaths::FileExists(IniPath))
	{
		WriteCompiledTagsIni();
	}
	RebuildNativeTags();
	UE_LOG(LogSimpleQuest, Display, TEXT("SimpleQuestEditor: tag registration complete (%d graph(s) in registry)"), CompiledTagRegistry.Num());
}

void FSimpleQuestEditor::OnAssetRemoved(const FAssetData& AssetData)
{
	if (AssetData.AssetClassPath != UQuestlineGraph::StaticClass()->GetClassPathName()) return;

	const FString RemovedPath = AssetData.GetObjectPathString();
	if (CompiledTagRegistry.Remove(RemovedPath) > 0)
	{
		UE_LOG(LogSimpleQuest, Display, TEXT("FSimpleQuestEditor::OnAssetRemoved — removed %s from tag registry, rewriting INI"), *AssetData.AssetName.ToString());
		WriteCompiledTagsIni();
		RebuildNativeTags();
	}
}

void FSimpleQuestEditor::ShutdownModule()
{
	FEditorDelegates::MapChange.RemoveAll(this);
	FEditorDelegates::PreBeginPIE.RemoveAll(this);
	
	if (FModuleManager::Get().IsModuleLoaded("AssetRegistry"))
	{
		FModuleManager::GetModuleChecked<FAssetRegistryModule>("AssetRegistry").Get().OnFilesLoaded().RemoveAll(this);
		FModuleManager::GetModuleChecked<FAssetRegistryModule>("AssetRegistry").Get().OnAssetRemoved().RemoveAll(this);
	}

	FQuestlineGraphEditorCommands::Unregister();
	
	if (FModuleManager::Get().IsModuleLoaded("AssetTools"))
	{
		IAssetTools& AssetTools = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools").Get();
		AssetTools.UnregisterAssetTypeActions(QuestlineGraphAssetTypeActions.ToSharedRef());
	}
	FEdGraphUtilities::UnregisterVisualNodeFactory(QuestlineGraphNodeFactory);
	QuestlineGraphNodeFactory.Reset();

	FEdGraphUtilities::UnregisterVisualPinConnectionFactory(QuestlineConnectionFactory);
	QuestlineConnectionFactory.Reset();
	
	QuestlineGraphAssetTypeActions.Reset();
}

void FSimpleQuestEditor::RegisterCompilerFactory(FQuestlineCompilerFactoryDelegate InFactory)
{
	CompilerFactory = MoveTemp(InFactory);
}

void FSimpleQuestEditor::UnregisterCompilerFactory()
{
	CompilerFactory.Unbind();
}

TUniquePtr<FQuestlineGraphCompiler> FSimpleQuestEditor::CreateCompiler() const
{
	if (CompilerFactory.IsBound())
	{
		TUniquePtr<FQuestlineGraphCompiler> CustomCompiler = CompilerFactory.Execute();
		if (CustomCompiler.IsValid())
		{
			return CustomCompiler;
		}
		UE_LOG(LogTemp, Warning, TEXT("FSimpleQuestEditor::CreateCompiler : Registered factory returned null. Falling back to default compiler."));
	}
	return MakeUnique<FQuestlineGraphCompiler>();
}

void FSimpleQuestEditor::RegisterCompiledTags(const FString& GraphPath, const TArray<FName>& TagNames)
{
	UE_LOG(LogSimpleQuest, Display, TEXT("FSimpleQuestEditor::RegisterCompiledTags — %s (%d tag(s))"), *GraphPath, TagNames.Num());
	CompiledTagRegistry.Add(GraphPath, TagNames);
	WriteCompiledTagsIni();
	RebuildNativeTags();
}

void FSimpleQuestEditor::WriteCompiledTagsIni() const
{
	// Collect and expand all tags from all registered graphs. Each quest node tag generates five state fact tags alongside itself.
	TArray<FName> AllTags;
	for (const auto& Pair : CompiledTagRegistry)
	{
		for (const FName& QuestTag : Pair.Value)
		{
			AllTags.Add(QuestTag);
			AllTags.Add(QuestStateTagUtils::MakeStateFact(QuestTag, QuestStateTagUtils::Leaf_Active));
			AllTags.Add(QuestStateTagUtils::MakeStateFact(QuestTag, QuestStateTagUtils::Leaf_Completed));
			AllTags.Add(QuestStateTagUtils::MakeStateFact(QuestTag, QuestStateTagUtils::Leaf_PendingGiver));
			AllTags.Add(QuestStateTagUtils::MakeStateFact(QuestTag, QuestStateTagUtils::Leaf_Deactivated));
			AllTags.Add(QuestStateTagUtils::MakeStateFact(QuestTag, QuestStateTagUtils::Leaf_Blocked));
		}
	}

	// Deduplicate and sort for stable VCS diffs.
	AllTags.Sort([](const FName& A, const FName& B) { return A.LexicalLess(B); });
	{
		int32 WriteIdx = 0;
		for (int32 ReadIdx = 0; ReadIdx < AllTags.Num(); ++ReadIdx)
		{
			if (ReadIdx == 0 || AllTags[ReadIdx] != AllTags[WriteIdx - 1])
				AllTags[WriteIdx++] = AllTags[ReadIdx];
		}
		AllTags.SetNum(WriteIdx);
	}

	// Build INI content in the standard [/Script/GameplayTags.GameplayTagsList] format. UE scans Config/Tags/*.ini natively at startup before any map load.
	FString IniContent;
	IniContent.Reserve(AllTags.Num() * 80);
	IniContent += TEXT("[/Script/GameplayTags.GameplayTagsList]\n");
	for (const FName& TagName : AllTags)
	{
		IniContent += FString::Printf(TEXT("+GameplayTagList=(Tag=\"%s\",DevComment=\"SimpleQuest\")\n"), *TagName.ToString());
	}

	const FString IniPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectConfigDir() / TEXT("Tags/SimpleQuestCompiledTags.ini"));
	const FString IniDir  = FPaths::GetPath(IniPath);

	UE_LOG(LogSimpleQuest, Display, TEXT("FSimpleQuestEditor::WriteCompiledTagsIni — writing %d tag(s) to: %s"), AllTags.Num(), *IniPath);

	if (!IFileManager::Get().DirectoryExists(*IniDir))
	{
		IFileManager::Get().MakeDirectory(*IniDir, /*Tree=*/true);
	}

	if (FFileHelper::SaveStringToFile(IniContent, *IniPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(LogSimpleQuest, Display, TEXT("FSimpleQuestEditor::WriteCompiledTagsIni — write succeeded"));
	}
	else
	{
		UE_LOG(LogSimpleQuest, Error, TEXT("FSimpleQuestEditor::WriteCompiledTagsIni — write FAILED for: %s"), *IniPath);
	}
}

void FSimpleQuestEditor::RebuildNativeTags()
{
	// Destroy old instances first — FNativeGameplayTag deregisters itself from the native tag list on destruction, so Reset()
	// cleanly removes all previously registered compiled tags.
	CompiledNativeTags.Reset();

	for (const auto& Pair : CompiledTagRegistry)
	{
		for (const FName& QuestTag : Pair.Value)
		{
			auto Add = [this](FName TagName)
			{
				CompiledNativeTags.Add(MakeUnique<FNativeGameplayTag>(
					FName("SimpleQuest"), FName("SimpleQuest"), TagName, TEXT(""),
					ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD));
			};
			Add(QuestTag);
			Add(QuestStateTagUtils::MakeStateFact(QuestTag, QuestStateTagUtils::Leaf_Active));
			Add(QuestStateTagUtils::MakeStateFact(QuestTag, QuestStateTagUtils::Leaf_Completed));
			Add(QuestStateTagUtils::MakeStateFact(QuestTag, QuestStateTagUtils::Leaf_PendingGiver));
			Add(QuestStateTagUtils::MakeStateFact(QuestTag, QuestStateTagUtils::Leaf_Deactivated));
			Add(QuestStateTagUtils::MakeStateFact(QuestTag, QuestStateTagUtils::Leaf_Blocked));
		}
	}

	// ConstructGameplayTagTree rebuilds the tree from all registered sources (including the FNativeGameplayTag instances we
	// just created) WITHOUT calling ResetTagCache() first. EditorRefreshGameplayTagTree() does call ResetTagCache(), which
	// clears the entire tag container before rebuilding — if anything races against that clear the freshly registered native
	// tags can be lost. ConstructGameplayTagTree() is the safe call here.
	UGameplayTagsManager::Get().ConstructGameplayTagTree();

	UE_LOG(LogSimpleQuest, Display, TEXT("FSimpleQuestEditor::RebuildNativeTags — registered %d native tag(s)"), CompiledNativeTags.Num());
}

