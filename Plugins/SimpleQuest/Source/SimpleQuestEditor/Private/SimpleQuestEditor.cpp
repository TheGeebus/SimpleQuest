// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "SimpleQuestEditor.h"
#include "AssetTypes/QuestlineGraphAssetTypeActions.h"
#include "AssetToolsModule.h"
#include "Modules/ModuleManager.h"
#include "EdGraphUtilities.h"
#include "GameplayTagsManager.h"
#include "MessageLogModule.h"
#include "Utilities/QuestlineGraphCompiler.h"
#include "SGraphNodeKnot.h"
#include "SGraphPin.h"
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
#include "Framework/Notifications/NotificationManager.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "K2Nodes/K2Node_CompleteObjectiveWithOutcome.h"
#include "Nodes/QuestlineNode_Step.h"
#include "Nodes/Prerequisites/QuestlineNode_PrerequisiteAnd.h"
#include "Nodes/Prerequisites/QuestlineNode_PrerequisiteBase.h"
#include "Nodes/Prerequisites/QuestlineNode_PrerequisiteNot.h"
#include "Nodes/Prerequisites/QuestlineNode_PrerequisiteOr.h"
#include "Nodes/Slate/SGraphNode_PrerequisiteCombinator.h"
#include "Nodes/Slate/SGraphNode_QuestlineStep.h"
#include "UObject/SavePackage.h"
#include "Utilities/SimpleQuestEditorUtils.h"
#include "Widgets/Notifications/SNotificationList.h"


IMPLEMENT_MODULE(FSimpleQuestEditor, SimpleQuestEditor);

FString FSimpleQuestEditor::GetCompiledTagsIniPath()
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectConfigDir() / TEXT("SimpleQuest/CompiledTags.ini"));
}

class FQuestlineGraphNodeFactory : public FGraphPanelNodeFactory
{
	virtual TSharedPtr<SGraphNode> CreateNode(UEdGraphNode* Node) const override
	{
		if (UQuestlineNode_Knot* KnotNode = Cast<UQuestlineNode_Knot>(Node))
		{
			return SNew(SGraphNodeKnot, KnotNode);
		}
		if (Cast<UQuestlineNode_PrerequisiteAnd>(Node)
			|| Cast<UQuestlineNode_PrerequisiteOr>(Node)
			|| Cast<UQuestlineNode_PrerequisiteNot>(Node))
		{
			return SNew(SGraphNode_PrerequisiteCombinator, CastChecked<UQuestlineNode_PrerequisiteBase>(Node));
		}
		if (UQuestlineNode_Step* StepNode = Cast<UQuestlineNode_Step>(Node))
		{
			return SNew(SGraphNode_QuestlineStep, StepNode);
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

	/*
	QuestlineK2PinFactory = MakeShared<FQuestlineK2PinFactory>();
	FEdGraphUtilities::RegisterVisualPinFactory(QuestlineK2PinFactory);
	*/
	
	FQuestlineGraphEditorCommands::Register();

	FMessageLogModule& MessageLogModule = FModuleManager::LoadModuleChecked<FMessageLogModule>("MessageLog");
	MessageLogModule.RegisterLogListing("QuestCompiler", NSLOCTEXT("SimpleQuestEditor", "QuestCompilerLog", "Quest Compiler"));

	// ── Early tag registration from compiled INI ──────────────────────
	// Creates native tags BEFORE the Asset Registry finishes loading, ensuring quest tags are available for asset deserialization.
	// The INI lives outside Config/Tags/ so the Gameplay Tag editor's save picker never sees it.
	LoadCompiledTagsFromIni();
	MigrateLegacyTagsIni();
	
	FAssetRegistryModule& ARModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AR = ARModule.Get();
	
	// Always subscribe — OnFilesLoaded fires when async loading finishes, whenever that is. If the AR is already done loading
	// when we subscribe, the delegate will not fire, so we also call immediately as a fallback for that case.
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

		CompiledTagRegistry.Add(AssetData.PackageName.ToString(), MoveTemp(TagNames));
		UE_LOG(LogSimpleQuest, Display, TEXT("  %s — loaded %d tag(s)"), *AssetData.AssetName.ToString(), TagStrings.Num());
	}
	
	// The INI is the authoritative compiled state. Only generate it from AR metadata when it doesn't already exist (first
	// run, deleted file). After that it is exclusively maintained by RegisterCompiledTags at compile time. Writing it unconditionally
	// here would overwrite a correctly-compiled INI with stale AR metadata whenever a graph was compiled but not saved.
	const FString IniPath = GetCompiledTagsIniPath();
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

	const FString RemovedPath = AssetData.PackageName.ToString();
	if (CompiledTagRegistry.Remove(RemovedPath) > 0)
	{
		UE_LOG(LogSimpleQuest, Display, TEXT("FSimpleQuestEditor::OnAssetRemoved — removed %s from tag registry, rewriting INI"), *AssetData.AssetName.ToString());
		WriteCompiledTagsIni();
		RebuildNativeTags(true);
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

	if (FModuleManager::Get().IsModuleLoaded("MessageLog"))
	{
		FMessageLogModule& MessageLogModule = FModuleManager::GetModuleChecked<FMessageLogModule>("MessageLog");
		MessageLogModule.UnregisterLogListing("QuestCompiler");
	}

	FQuestlineGraphEditorCommands::Unregister();

	/*
	if (QuestlineK2PinFactory.IsValid())
	{
		FEdGraphUtilities::UnregisterVisualPinFactory(QuestlineK2PinFactory);
	}
	*/

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
	// Detect stale tags: tags in the old compiled set that are absent from the new set
	bool bHasStaleTags = false;
	if (const TArray<FName>* OldTags = CompiledTagRegistry.Find(GraphPath))
	{
		TSet<FName> NewTagSet(TagNames);
		for (const FName& OldTag : *OldTags)
		{
			if (!NewTagSet.Contains(OldTag))
			{
				bHasStaleTags = true;
				UE_LOG(LogSimpleQuest, Display, TEXT("  Stale tag removed: %s"), *OldTag.ToString());
			}
		}
	}

	UE_LOG(LogSimpleQuest, Display, TEXT("FSimpleQuestEditor::RegisterCompiledTags — %s (%d tag(s)%s)"),
		*GraphPath, TagNames.Num(), bHasStaleTags ? TEXT(", stale tags cleaned") : TEXT(""));

	CompiledTagRegistry.Add(GraphPath, TagNames);
	WriteCompiledTagsIni();
	RebuildNativeTags(bHasStaleTags);
}

void FSimpleQuestEditor::CompileAllQuestlineGraphs()
{
    IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();

    FARFilter Filter;
    Filter.ClassPaths.Add(UQuestlineGraph::StaticClass()->GetClassPathName());
    Filter.bRecursiveClasses = true;

    TArray<FAssetData> QuestlineAssets;
    AR.GetAssets(Filter, QuestlineAssets);

    if (QuestlineAssets.IsEmpty())
    {
        // Notification: "No questline graphs found."
        return;
    }

    FScopedSlowTask SlowTask(QuestlineAssets.Num(), NSLOCTEXT("SimpleQuestEditor", "CompileAll_Progress", "Compiling questline graphs..."));
    SlowTask.MakeDialog(/*bShowCancelButton=*/ true);

    int32 SuccessCount = 0;
    int32 FailCount = 0;
    int32 TotalErrors = 0;
    int32 TotalWarnings = 0;
    FMessageLog CompilerLog("QuestCompiler");
	CompilerLog.NewPage(NSLOCTEXT("SimpleQuestEditor", "CompileAllPageLabel", "Compile All"));

    int32 TotalRenames = 0;
    int32 TotalRenamedActors = 0;

    for (const FAssetData& AssetData : QuestlineAssets)
    {
        if (SlowTask.ShouldCancel()) break;

        SlowTask.EnterProgressFrame(1.f, FText::Format(NSLOCTEXT("SimpleQuestEditor", "CompileAll_Item", "Compiling {0}..."), FText::FromName(AssetData.AssetName)));

        UQuestlineGraph* Graph = Cast<UQuestlineGraph>(AssetData.GetAsset());
        if (!Graph) { ++FailCount; continue; }
    	
        TUniquePtr<FQuestlineGraphCompiler> Compiler = CreateCompiler();
        if (Compiler->Compile(Graph))
        {
            ++SuccessCount;

            // Propagate renames BEFORE broadcast — OnExternalCompile triggers RefreshAllNodeWidgets, which queries actors by compiled tag.
            // Actors must already hold the new tags for those queries to succeed.
            const TMap<FName, FName>& DetectedRenames = Compiler->GetDetectedRenames();
            if (DetectedRenames.Num() > 0)
            {
                TotalRenamedActors += USimpleQuestEditorUtilities::ApplyTagRenamesToLoadedWorlds(DetectedRenames);
                TotalRenames += DetectedRenames.Num();
            }

            UPackage* Package = Graph->GetOutermost();
            Package->MarkPackageDirty();
            FSavePackageArgs Args;
            UPackage::SavePackage(Package, Graph, *FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension()), Args);
            QuestlineCompiledDelegate.Broadcast(Package->GetName(), true);
        }
        else
        {
            ++FailCount;
            QuestlineCompiledDelegate.Broadcast(Graph->GetOutermost()->GetName(), false);
        }

    	// Add messages directly — no per-graph NewPage
    	if (Compiler->GetMessages().Num() > 0)
    	{
    		CompilerLog.AddMessages(Compiler->GetMessages());
    		TotalErrors += Compiler->GetNumErrors();
    		TotalWarnings += Compiler->GetNumWarnings();
    	}
    }

    // Summary notification
    if (TotalErrors > 0 || TotalWarnings > 0)
    {
        // Clickable toast — opens the MessageLog with all per-graph pages
        CompilerLog.Notify(FText::Format(NSLOCTEXT("SimpleQuestEditor", "CompileAll_Issues", "Compiled {0} questline(s): {1} error(s), {2} warning(s)"),
            FText::AsNumber(SuccessCount + FailCount),
            FText::AsNumber(TotalErrors),
            FText::AsNumber(TotalWarnings)));
    }
    else
    {
        // Clean run — simple success toast
        const FText Summary = FText::Format(NSLOCTEXT("SimpleQuestEditor", "CompileAll_Summary", "Compiled {0} questline(s) successfully"), FText::AsNumber(SuccessCount + FailCount));
        FNotificationInfo Info(Summary);
        Info.ExpireDuration = 5.f;
        Info.bUseSuccessFailIcons = true;
        FSlateNotificationManager::Get().AddNotification(Info)->SetCompletionState(SNotificationItem::CS_Success);
    }

	// Tag rename toast
	if (TotalRenames > 0)
	{
		FNotificationInfo RenameInfo(FText::Format(NSLOCTEXT("SimpleQuestEditor", "CompileAll_TagRenames", "{0} tag(s) renamed. {1} actor(s) updated in loaded levels."), TotalRenames, TotalRenamedActors));
		RenameInfo.ExpireDuration = 5.f;
		RenameInfo.bUseSuccessFailIcons = true;
		FSlateNotificationManager::Get().AddNotification(RenameInfo)->SetCompletionState(SNotificationItem::CS_Success);
	}
}

void FSimpleQuestEditor::LoadCompiledTagsFromIni()
{
	const FString IniPath = GetCompiledTagsIniPath();
	FString Content;
	if (!FFileHelper::LoadFileToString(Content, *IniPath))
	{
		UE_LOG(LogSimpleQuest, Display, TEXT("LoadCompiledTagsFromIni — no INI found (first run or not yet compiled)"));
		return;
	}

	TArray<FString> Lines;
	Content.ParseIntoArrayLines(Lines);
	for (const FString& Line : Lines)
	{
		const int32 TagStart = Line.Find(TEXT("Tag=\""));
		if (TagStart == INDEX_NONE) continue;
		const int32 ValueStart = TagStart + 5;
		const int32 ValueEnd = Line.Find(TEXT("\""), ESearchCase::IgnoreCase,	ESearchDir::FromStart, ValueStart);
		if (ValueEnd != INDEX_NONE)
		{
			const FName TagName(*Line.Mid(ValueStart, ValueEnd - ValueStart));
			CompiledNativeTags.Add(MakeUnique<FNativeGameplayTag>(FName("SimpleQuest"), FName("SimpleQuest"),
				TagName, TEXT(""), ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD));
		}
	}

	if (CompiledNativeTags.Num() > 0)
	{
		UGameplayTagsManager::Get().ConstructGameplayTagTree();
	}

	UE_LOG(LogSimpleQuest, Display, TEXT("LoadCompiledTagsFromIni — registered %d native tag(s)"), CompiledNativeTags.Num());
}

void FSimpleQuestEditor::MigrateLegacyTagsIni()
{
	const FString LegacyPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectConfigDir() / TEXT("Tags/SimpleQuestCompiledTags.ini"));
	if (FPaths::FileExists(LegacyPath))
	{
		IFileManager::Get().Delete(*LegacyPath);
		UE_LOG(LogSimpleQuest, Display,	TEXT("MigrateLegacyTagsIni — deleted legacy INI at Config/Tags/. Tags now managed via Config/SimpleQuest/."));
	}
}

void FSimpleQuestEditor::WriteCompiledTagsIni() const
{
    const FString IniPath = GetCompiledTagsIniPath();

    // Collect compiled tags and state facts from registry
    TArray<FName> AllTags;
    for (const auto& Pair : CompiledTagRegistry)
    {
        for (const FName& QuestTag : Pair.Value)
        {
            AllTags.Add(QuestTag);
            if (!QuestTag.ToString().StartsWith(UQuestStateTagUtils::Namespace)  && !QuestTag.ToString().StartsWith(TEXT("Quest.Outcome.")))
            {
                auto AddState = [&](const FString& Leaf)
                {
                    AllTags.Add(UQuestStateTagUtils::MakeStateFact(QuestTag, Leaf));
                };
                AddState(UQuestStateTagUtils::Leaf_Active);
                AddState(UQuestStateTagUtils::Leaf_Completed);
                AddState(UQuestStateTagUtils::Leaf_PendingGiver);
                AddState(UQuestStateTagUtils::Leaf_Deactivated);
                AddState(UQuestStateTagUtils::Leaf_Blocked);
            }
        }
    }

    // Deduplicate and sort
    AllTags.Sort([](const FName& A, const FName& B) { return A.LexicalLess(B); });
    {
        int32 WriteIdx = 0;
        for (int32 ReadIdx = 0; ReadIdx < AllTags.Num(); ++ReadIdx)
        {
	        if (ReadIdx == 0 || AllTags[ReadIdx] != AllTags[WriteIdx - 1])
	        {
	        	AllTags[WriteIdx++] = AllTags[ReadIdx];
	        }
        }
        AllTags.SetNum(WriteIdx);
    }

    FString IniContent;
    IniContent.Reserve(AllTags.Num() * 80 + 512);
    IniContent += TEXT("; ==========================================================================\n");
    IniContent += TEXT("; AUTO-GENERATED by SimpleQuest compiler. DO NOT EDIT.\n");
    IniContent += TEXT(";\n");
    IniContent += TEXT("; Overwritten on every compile. Manual edits will be lost.\n");
    IniContent += TEXT("; For custom gameplay tags, use Config/Tags/DefaultGameplayTags.ini.\n");
    IniContent += TEXT("; ==========================================================================\n");
    IniContent += TEXT("[/Script/GameplayTags.GameplayTagsList]\n");
    for (const FName& TagName : AllTags)
    {
        IniContent += FString::Printf(TEXT("+GameplayTagList=(Tag=\"%s\",DevComment=\"SimpleQuest\")\n"), *TagName.ToString());
    }

    const FString IniDir = FPaths::GetPath(IniPath);
    if (!IFileManager::Get().DirectoryExists(*IniDir))
    {
        IFileManager::Get().MakeDirectory(*IniDir, true);
    }

    if (FFileHelper::SaveStringToFile(IniContent, *IniPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    {
        UE_LOG(LogSimpleQuest, Display, TEXT("FSimpleQuestEditor::WriteCompiledTagsIni — wrote %d tag(s) to: %s"), AllTags.Num(), *IniPath);
    }
    else
    {
        UE_LOG(LogSimpleQuest, Error, TEXT("FSimpleQuestEditor::WriteCompiledTagsIni — write FAILED for: %s"), *IniPath);
    }
}

void FSimpleQuestEditor::RebuildNativeTags(bool bRefreshTree)
{
	CompiledNativeTags.Reset();

	for (const auto& Pair : CompiledTagRegistry)
	{
		for (const FName& QuestTag : Pair.Value)
		{
			auto Add = [this](FName TagName)
			{
				CompiledNativeTags.Add(MakeUnique<FNativeGameplayTag>(FName("SimpleQuest"), FName("SimpleQuest"),
					TagName, TEXT(""), ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD));
			};
			Add(QuestTag);
			if (!QuestTag.ToString().StartsWith(UQuestStateTagUtils::Namespace) && !QuestTag.ToString().StartsWith(TEXT("Quest.Outcome.")))
			{
				Add(UQuestStateTagUtils::MakeStateFact(QuestTag, UQuestStateTagUtils::Leaf_Active));
				Add(UQuestStateTagUtils::MakeStateFact(QuestTag, UQuestStateTagUtils::Leaf_Completed));
				Add(UQuestStateTagUtils::MakeStateFact(QuestTag, UQuestStateTagUtils::Leaf_PendingGiver));
				Add(UQuestStateTagUtils::MakeStateFact(QuestTag, UQuestStateTagUtils::Leaf_Deactivated));
				Add(UQuestStateTagUtils::MakeStateFact(QuestTag, UQuestStateTagUtils::Leaf_Blocked));
			}
		}
	}

	if (bRefreshTree)
	{
		// Full tree teardown and rebuild to prune stale tags from the in-memory tree.
		// Safe here: runs synchronously during compilation which means no concurrent tag requests.
		UGameplayTagsManager::Get().EditorRefreshGameplayTagTree();
	}
	else
	{
		// Additive rebuild. Safe for startup and non-stale-removal cases where the original race concern (ResetTagCache clearing
		// freshly registered native tags) applies.
		UGameplayTagsManager::Get().ConstructGameplayTagTree();
	}

	UE_LOG(LogSimpleQuest, Display, TEXT("FSimpleQuestEditor::RebuildNativeTags — registered %d native tag(s)%s"),
		CompiledNativeTags.Num(), bRefreshTree ? TEXT(" (tree refreshed)") : TEXT(""));
}

