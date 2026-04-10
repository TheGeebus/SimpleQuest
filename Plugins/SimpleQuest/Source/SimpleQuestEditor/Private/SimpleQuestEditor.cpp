// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "SimpleQuestEditor.h"
#include "AssetTypes/QuestlineGraphAssetTypeActions.h"
#include "AssetToolsModule.h"
#include "Modules/ModuleManager.h"
#include "EdGraphUtilities.h"
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

	FCoreDelegates::OnPostEngineInit.AddRaw(this, &FSimpleQuestEditor::OnPostEngineInit);

	FAssetRegistryModule& ARModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AR = ARModule.Get();
	if (AR.IsLoadingAssets())
	{
		AR.OnFilesLoaded().AddRaw(this, &FSimpleQuestEditor::RegisterTagsFromAssetRegistry);
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

void FSimpleQuestEditor::OnPostEngineInit()
{
	FAssetRegistryModule& ARModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AR = ARModule.Get();
	AR.WaitForCompletion();
	RegisterTagsFromAssetRegistry();
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

	UE_LOG(LogTemp, Display, TEXT("SimpleQuestEditor: RegisterTagsFromAssetRegistry — found %d questline asset(s)"), QuestlineAssets.Num());

	for (const FAssetData& AssetData : QuestlineAssets)
	{
		FAssetTagValueRef TagValue = AssetData.TagsAndValues.FindTag(TEXT("CompiledQuestTags"));

		UE_LOG(LogTemp, Display, TEXT("  Asset: %s | CompiledQuestTags set: %s | Value: %s"),
			*AssetData.AssetName.ToString(),
			TagValue.IsSet() ? TEXT("yes") : TEXT("no"),
			TagValue.IsSet() ? *TagValue.GetValue() : TEXT("(none)"));

		if (!TagValue.IsSet() || TagValue.GetValue().IsEmpty())
		{
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

		RegisterCompiledTags(AssetData.GetObjectPathString(), TagNames);
		UE_LOG(LogTemp, Display, TEXT("  Registered %d tag(s) from %s"), TagNames.Num(), *AssetData.AssetName.ToString());
	}
}

void FSimpleQuestEditor::ShutdownModule()
{
	FEditorDelegates::MapChange.RemoveAll(this);
	FEditorDelegates::PreBeginPIE.RemoveAll(this);
	FCoreDelegates::OnPostEngineInit.RemoveAll(this);
	
	if (FModuleManager::Get().IsModuleLoaded("AssetRegistry"))
	{
		FModuleManager::GetModuleChecked<FAssetRegistryModule>("AssetRegistry").Get().OnFilesLoaded().RemoveAll(this);
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
	CompiledTagRegistry.Remove(GraphPath);
	TArray<TUniquePtr<FNativeGameplayTag>>& Entries = CompiledTagRegistry.Add(GraphPath);

	TArray<FName> AllTags = TagNames;
	for (const FName& QuestTag : TagNames)
	{
		AllTags.Add(QuestStateTagUtils::MakeStateFact(QuestTag, QuestStateTagUtils::Leaf_Active));
		AllTags.Add(QuestStateTagUtils::MakeStateFact(QuestTag, QuestStateTagUtils::Leaf_Completed));
		AllTags.Add(QuestStateTagUtils::MakeStateFact(QuestTag, QuestStateTagUtils::Leaf_PendingGiver));
		AllTags.Add(QuestStateTagUtils::MakeStateFact(QuestTag, QuestStateTagUtils::Leaf_Deactivated));
		AllTags.Add(QuestStateTagUtils::MakeStateFact(QuestTag, QuestStateTagUtils::Leaf_Blocked));
	}

	for (const FName& TagName : AllTags)
	{
		Entries.Add(MakeUnique<FNativeGameplayTag>(FName("SimpleQuest"), FName("SimpleQuest"), TagName, TEXT(""),
			ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD));
	}

	UGameplayTagsManager::Get().ConstructGameplayTagTree();
}


/*
class FQuestlineConnectionFactory : public FGraphPanelPinConnectionFactory
{
public:
	virtual FConnectionDrawingPolicy* CreateConnectionPolicy(
		const UEdGraphSchema* Schema,
		int32 InBackLayerID, int32 InFrontLayerID,
		float InZoomFactor,
		const FSlateRect& InClippingRect,
		FSlateWindowElementList& InDrawElements,
		UEdGraph* InGraphObj) const override
	{
		if (!Cast<UQuestlineGraphSchema>(Schema))
			return nullptr;

		// Ask every OTHER registered factory for a policy (picks up Electronic Nodes, etc.)
		FConnectionDrawingPolicy* InnerPolicy = nullptr;
		for (const TSharedPtr<FGraphPanelPinConnectionFactory>& OtherFactory
				: FEdGraphUtilities::VisualPinConnectionFactories)
		{
			if (OtherFactory.Get() == this) continue;
			InnerPolicy = OtherFactory->CreateConnectionPolicy(
				Schema, InBackLayerID, InFrontLayerID,
				InZoomFactor, InClippingRect, InDrawElements, InGraphObj);
			if (InnerPolicy) break;
		}

		// Fall back to the schema's own policy if no other factory handled it
		if (!InnerPolicy)
		{
			InnerPolicy = Schema->CreateConnectionDrawingPolicy(
				InBackLayerID, InFrontLayerID,
				InZoomFactor, InClippingRect, InDrawElements, InGraphObj);
		}

		return new FQuestlineDashOverlayPolicy(
			InnerPolicy,
			InBackLayerID, InFrontLayerID,
			InZoomFactor, InClippingRect, InDrawElements);
	}
};
*/
