// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Nodes/QuestlineNode_Entry.h"
#include "Nodes/QuestlineNode_Quest.h"
#include "Nodes/QuestlineNode_LinkedQuestline.h"
#include "Quests/QuestlineGraph.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Utilities/SimpleQuestEditorUtils.h"
#include "Widgets/Notifications/SNotificationList.h"


void UQuestlineNode_Entry::AllocateDefaultPins()
{
	// Named outcome paths — fire only when this graph was entered via that specific outcome
	for (const FGameplayTag& Tag : IncomingOutcomeTags)
	{
		if (Tag.IsValid()) CreatePin(EGPD_Output, TEXT("QuestOutcome"), Tag.GetTagName());
	}

	// Unconditional path — always fires when this graph activates
	CreatePin(EGPD_Output, TEXT("QuestActivation"), TEXT("Any Outcome"));

	// Deactivation — fires when the parent Quest node is deactivated
	if (bShowDeactivationPins)
	{
		CreatePin(EGPD_Output, TEXT("QuestDeactivated"), TEXT("Deactivated"));
	}
}

void UQuestlineNode_Entry::RefreshOutcomePins()
{
	TArray<FName> DesiredNames;
	for (const FGameplayTag& Tag : IncomingOutcomeTags)
	{
		if (Tag.IsValid()) DesiredNames.Add(Tag.GetTagName());
	}
	SyncPinsByCategory(EGPD_Output, TEXT("QuestOutcome"), DesiredNames, {});
}

void UQuestlineNode_Entry::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UQuestlineNode_Entry, IncomingOutcomeTags))
	{
		RefreshOutcomePins();
	}
	
	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UQuestlineNode_Entry, bShowDeactivationPins))
	{
		Modify();

		if (bShowDeactivationPins)
		{
			if (UEdGraphPin* Pin = FindPin(TEXT("Deactivated")))
			{
				Pin->bOrphanedPin = false;
			}
			else
			{
				CreatePin(EGPD_Output, TEXT("QuestDeactivated"), TEXT("Deactivated"));
			}
		}
		else
		{
			if (UEdGraphPin* Pin = FindPin(TEXT("Deactivated")))
			{
				if (Pin->LinkedTo.Num() > 0)
				{
					Pin->bOrphanedPin = true;
				}
				else
				{
					Pin->BreakAllPinLinks(); RemovePin(Pin);
				}
			}
		}

		if (UEdGraph* Graph = GetGraph())
		{
			Graph->NotifyGraphChanged();
		}
	}
}

void UQuestlineNode_Entry::GetNodeContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const
{
	Super::GetNodeContextMenuActions(Menu, Context);

	FToolMenuSection& Section = Menu->AddSection(TEXT("EntryNode"), NSLOCTEXT("SimpleQuestEditor", "EntryNodeSection", "Entry"));

	Section.AddMenuEntry(
		TEXT("ImportOutcomePins"),
		NSLOCTEXT("SimpleQuestEditor", "ImportOutcomePins_Label", "Import Outcome Pins"),
		NSLOCTEXT("SimpleQuestEditor", "ImportOutcomePins_Tooltip",
			"Scan parent graphs for outcome tags and import them as incoming outcome pins"),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([NodePtr = const_cast<UQuestlineNode_Entry*>(this)]()
		{
			NodePtr->ImportOutcomePinsFromParent();
		}))
	);
}

void UQuestlineNode_Entry::ImportOutcomePinsFromParent()
{
	TArray<FName> FoundOutcomeNames;

	UObject* GraphOuter = GetGraph() ? GetGraph()->GetOuter() : nullptr;
	if (!GraphOuter) return;

	// ── Case 1: inner graph owned by a Quest node ──────────────────────────
	if (UQuestlineNode_Quest* ParentQuestNode = Cast<UQuestlineNode_Quest>(GraphOuter))
	{
		UEdGraph* ParentGraph = ParentQuestNode->GetGraph();
		if (ParentGraph)
		{
			for (const UEdGraphNode* Node : ParentGraph->Nodes)
			{
				if (Node == ParentQuestNode) continue; // skip ourselves

				for (const UEdGraphPin* Pin : Node->Pins)
				{
					if (Pin->Direction == EGPD_Output
						&& Pin->PinType.PinCategory == TEXT("QuestOutcome")
						&& !Pin->PinName.IsNone())
					{
						FoundOutcomeNames.AddUnique(Pin->PinName);
					}
				}
			}
		}
	}
	// ── Case 2: top-level QuestlineGraph — AR scan for LinkedQuestline refs ─
	else if (UQuestlineGraph* QuestlineAsset = Cast<UQuestlineGraph>(GraphOuter))
	{
		const IAssetRegistry& AR =
			FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

		TArray<FAssetData> AllGraphAssets;
		AR.GetAssetsByClass(UQuestlineGraph::StaticClass()->GetClassPathName(), AllGraphAssets);

		const FSoftObjectPath OurPath(QuestlineAsset);

		for (const FAssetData& AssetData : AllGraphAssets)
		{
			UQuestlineGraph* OtherAsset = Cast<UQuestlineGraph>(AssetData.GetAsset());
			if (!OtherAsset || OtherAsset == QuestlineAsset || !OtherAsset->QuestlineEdGraph)
				continue;

			// Does any LinkedQuestline node in this graph reference our asset?
			bool bReferencesUs = false;
			for (const UEdGraphNode* Node : OtherAsset->QuestlineEdGraph->Nodes)
			{
				if (const UQuestlineNode_LinkedQuestline* LinkedNode =
					Cast<UQuestlineNode_LinkedQuestline>(Node))
				{
					if (LinkedNode->LinkedGraph.ToSoftObjectPath() == OurPath)
					{
						bReferencesUs = true;
						break;
					}
				}
			}

			if (!bReferencesUs) continue;

			// Collect QuestOutcome output pins from all content nodes in the parent
			for (const UEdGraphNode* Node : OtherAsset->QuestlineEdGraph->Nodes)
			{
				for (const UEdGraphPin* Pin : Node->Pins)
				{
					if (Pin->Direction == EGPD_Output
						&& Pin->PinType.PinCategory == TEXT("QuestOutcome")
						&& !Pin->PinName.IsNone())
					{
						FoundOutcomeNames.AddUnique(Pin->PinName);
					}
				}
			}
		}
	}

	// ── Nothing found ──────────────────────────────────────────────────────
	if (FoundOutcomeNames.IsEmpty())
	{
		FNotificationInfo Info(NSLOCTEXT("SimpleQuestEditor", "NoParentOutcomes",
			"No outcome tags found in parent graphs"));
		Info.ExpireDuration = 3.0f;
		FSlateNotificationManager::Get().AddNotification(Info);
		return;
	}

	// ── Merge into IncomingOutcomeTags ──────────────────────────────────────
	const FScopedTransaction Transaction(NSLOCTEXT("SimpleQuestEditor",
		"ImportOutcomePins_Undo", "Import Outcome Pins"));
	Modify();

	int32 ImportedCount = 0;
	for (const FName& OutcomeName : FoundOutcomeNames)
	{
		const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(OutcomeName, /*bErrorIfNotFound=*/ false);
		if (!Tag.IsValid()) continue;
		if (IncomingOutcomeTags.Contains(Tag)) continue;

		IncomingOutcomeTags.Add(Tag);
		ImportedCount++;
	}

	if (ImportedCount > 0)
	{
		RefreshOutcomePins();

		FNotificationInfo Info(FText::Format(
			NSLOCTEXT("SimpleQuestEditor", "ImportedOutcomes", "Imported {0} outcome pin(s)"),
			FText::AsNumber(ImportedCount)));
		Info.ExpireDuration = 3.0f;
		FSlateNotificationManager::Get().AddNotification(Info);
	}
	else
	{
		FNotificationInfo Info(NSLOCTEXT("SimpleQuestEditor", "AllOutcomesPresent",
			"All parent outcome tags already present"));
		Info.ExpireDuration = 3.0f;
		FSlateNotificationManager::Get().AddNotification(Info);
	}
}

FText UQuestlineNode_Entry::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return NSLOCTEXT("SimpleQuestEditor", "EntryNodeTitle", "Start");
}

FLinearColor UQuestlineNode_Entry::GetNodeTitleColor() const
{
	return SQ_ED_NODE_ENTRY;
}
