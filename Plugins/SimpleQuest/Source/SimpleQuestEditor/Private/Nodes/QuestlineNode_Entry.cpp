// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Nodes/QuestlineNode_Entry.h"
#include "Nodes/QuestlineNode_Quest.h"
#include "Nodes/QuestlineNode_LinkedQuestline.h"
#include "Quests/QuestlineGraph.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Utilities/SimpleQuestEditorUtils.h"
#include "Utilities/QuestlineGraphTraversalPolicy.h"
#include "Widgets/Notifications/SNotificationList.h"


void UQuestlineNode_Entry::AllocateDefaultPins()
{
	// Named outcome paths — fire only when this graph was entered via that specific outcome. Driven by exposed specs in
	// IncomingSignals. Specs with bExposed=false are stored but generate no pin (they persist the designer's selection
	// state across re-imports).
	for (const FIncomingSignalPinSpec& Spec : IncomingSignals)
	{
		if (!Spec.bExposed) continue;
		if (!Spec.Outcome.IsValid()) continue;
		CreatePin(EGPD_Output, TEXT("QuestOutcome"), Spec.Outcome.GetTagName());
	}

	// Unconditional path — always fires when this graph activates
	CreatePin(EGPD_Output, TEXT("QuestActivation"), TEXT("Any Outcome"));

	// Deactivation — fires when the parent Quest node is deactivated
	if (bShowDeactivationPins)
	{
		CreatePin(EGPD_Output, TEXT("QuestDeactivated"), TEXT("Deactivated"));
	}
}

void UQuestlineNode_Entry::PostLoad()
{
	Super::PostLoad();

	// Migrate legacy IncomingOutcomeTags array into IncomingSignals. Pre-Session-19 assets stored incoming outcomes as
	// a flat tag list with no source qualification; each becomes an exposed spec with no ParentAsset/SourceNodeGuid.
	// Session 20's source-aware import can enrich these later; in the meantime they function identically to the old pins.
	if (IncomingOutcomeTags_DEPRECATED.Num() > 0)
	{
		for (const FGameplayTag& Tag : IncomingOutcomeTags_DEPRECATED)
		{
			if (!Tag.IsValid()) continue;

			const bool bAlreadyPresent = IncomingSignals.ContainsByPredicate(
				[&Tag](const FIncomingSignalPinSpec& Existing)
				{
					return !Existing.SourceNodeGuid.IsValid()
						&& Existing.ParentAsset.IsNull()
						&& Existing.Outcome == Tag;
				});
			if (bAlreadyPresent) continue;

			FIncomingSignalPinSpec Spec;
			Spec.Outcome = Tag;
			Spec.bExposed = true;  // preserve existing visible pins; migrated outcomes stay exposed
			IncomingSignals.Add(Spec);
		}
		IncomingOutcomeTags_DEPRECATED.Empty();
	}

	// Sync pins to IncomingSignals. On fresh post-migration load, AllocateDefaultPins ran with an empty IncomingSignals
	// (migration hadn't happened yet), so pins need to be created now. SyncPinsByCategory preserves wiring on pins whose
	// names match (un-orphans wired-survivors that came in from serialization).
	RefreshOutcomePins();
}

void UQuestlineNode_Entry::RefreshOutcomePins()
{
	TArray<FName> DesiredNames;
	for (const FIncomingSignalPinSpec& Spec : IncomingSignals)
	{
		if (!Spec.bExposed) continue;
		if (!Spec.Outcome.IsValid()) continue;
		DesiredNames.Add(Spec.Outcome.GetTagName());
	}
	SyncPinsByCategory(EGPD_Output, TEXT("QuestOutcome"), DesiredNames, {});
}

void UQuestlineNode_Entry::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Use member-property name so edits to fields inside IncomingSignals[N] (e.g., bExposed toggle) register as IncomingSignals
	// changes. GetPropertyName() returns the innermost field name, which would miss nested-struct edits.
	const FName MemberName = PropertyChangedEvent.MemberProperty ? PropertyChangedEvent.MemberProperty->GetFName() : NAME_None;
	
	if (MemberName == GET_MEMBER_NAME_CHECKED(UQuestlineNode_Entry, IncomingSignals))
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

	FQuestlineGraphTraversalPolicy TraversalPolicy;

	// Walk back from a container node's Activate input and harvest only the outcome pins that actually feed it. Uses the
	// unified effective-source walker so indirect routes through knots, utility nodes, and activation group setter/getter
	// chains are captured too.
	auto CollectFromActivatePin = [&](const UEdGraphNode* ContainerNode)
	{
		const UEdGraphPin* ActivatePin = ContainerNode->FindPin(TEXT("Activate"), EGPD_Input);
		if (!ActivatePin) return;

		for (const UEdGraphPin* Wire : ActivatePin->LinkedTo)
		{
			TSet<const UEdGraphPin*> Sources;
			TSet<const UEdGraphNode*> Visited;
			TraversalPolicy.CollectEffectiveSources(Wire, Sources, Visited);
			for (const UEdGraphPin* Source : Sources)
			{
				// Only import specific named outcomes. AnyOutcome routes cannot distinguish which outcome fired at runtime,
				// so per-outcome entry pins would be dead; the AnyOutcome entry pin handles that path.
				if (Source->PinType.PinCategory == TEXT("QuestOutcome") && !Source->PinName.IsNone())
				{
					FoundOutcomeNames.AddUnique(Source->PinName);
				}
			}
		}
	};

	// Case 1: inner graph owned by a Quest node
	if (const UQuestlineNode_Quest* ParentQuestNode = Cast<UQuestlineNode_Quest>(GraphOuter))
	{
		CollectFromActivatePin(ParentQuestNode);
	}
	// Case 2: top-level QuestlineGraph — AR scan for LinkedQuestline refs
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

			for (const UEdGraphNode* Node : OtherAsset->QuestlineEdGraph->Nodes)
			{
				const UQuestlineNode_LinkedQuestline* LinkedNode =
					Cast<UQuestlineNode_LinkedQuestline>(Node);
				if (!LinkedNode || LinkedNode->LinkedGraph.ToSoftObjectPath() != OurPath) continue;
				CollectFromActivatePin(LinkedNode);
			}
		}
	}

	// Identify stale specs: previously-exposed outcomes that are no longer routed into the parent.
	TArray<int32> StaleIndices;
	for (int32 i = 0; i < IncomingSignals.Num(); ++i)
	{
		const FIncomingSignalPinSpec& Spec = IncomingSignals[i];
		if (!Spec.bExposed) continue;
		if (!Spec.Outcome.IsValid()) continue;
		if (!FoundOutcomeNames.Contains(Spec.Outcome.GetTagName()))
		{
			StaleIndices.Add(i);
		}
	}

	// Identify import actions: truly new tags get fresh specs; tags matching disabled specs get the existing spec re-enabled (no duplicates).
	TArray<FGameplayTag> NewOutcomeTags;
	TArray<int32> IndicesToReEnable;
	for (const FName& OutcomeName : FoundOutcomeNames)
	{
		const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(OutcomeName, /*bErrorIfNotFound=*/ false);
		if (!Tag.IsValid()) continue;

		int32 ExistingIndex = INDEX_NONE;
		for (int32 i = 0; i < IncomingSignals.Num(); ++i)
		{
			if (IncomingSignals[i].Outcome == Tag)
			{
				ExistingIndex = i;
				break;
			}
		}

		if (ExistingIndex == INDEX_NONE)
		{
			NewOutcomeTags.Add(Tag);
		}
		else if (!IncomingSignals[ExistingIndex].bExposed)
		{
			IndicesToReEnable.Add(ExistingIndex);
		}
		// else: already exposed and present — nothing to do
	}

	const bool bWouldImport = NewOutcomeTags.Num() > 0 || IndicesToReEnable.Num() > 0;
	const bool bWouldPrune  = StaleIndices.Num() > 0;
	if (!bWouldImport && !bWouldPrune)
	{
		FNotificationInfo Info(NSLOCTEXT("SimpleQuestEditor", "NoParentOutcomes",
			"No outcome tags are routed into this graph's parent Activate pin"));
		Info.ExpireDuration = 3.0f;
		FSlateNotificationManager::Get().AddNotification(Info);
		return;
	}

	const FScopedTransaction Transaction(NSLOCTEXT("SimpleQuestEditor",
		"ImportOutcomePins_Undo", "Import Outcome Pins"));
	Modify();

	// Prune stale: demote bExposed so the pin is removed but the spec is retained (designer selection state preserved for future re-imports).
	for (int32 i = StaleIndices.Num() - 1; i >= 0; --i)
	{
		IncomingSignals[StaleIndices[i]].bExposed = false;
	}

	// Re-enable disabled matches first (no new spec creation for these).
	for (int32 Index : IndicesToReEnable)
	{
		IncomingSignals[Index].bExposed = true;
	}

	// Append new specs for genuinely new outcomes.
	for (const FGameplayTag& Tag : NewOutcomeTags)
	{
		FIncomingSignalPinSpec Spec;
		Spec.Outcome = Tag;
		Spec.bExposed = true;
		IncomingSignals.Add(Spec);
	}

	const int32 ImportedCount = NewOutcomeTags.Num() + IndicesToReEnable.Num();

	if (ImportedCount > 0 || StaleIndices.Num() > 0)
	{
		RefreshOutcomePins();
	}

	if (ImportedCount > 0 && StaleIndices.Num() > 0)
	{
		FNotificationInfo Info(FText::Format(NSLOCTEXT("SimpleQuestEditor", "ImportedAndPrunedOutcomes",
			"Imported {0} outcome pin(s); flagged {1} stale pin(s) for cleanup"),
			FText::AsNumber(ImportedCount),
			FText::AsNumber(StaleIndices.Num())));
		Info.ExpireDuration = 3.5f;
		FSlateNotificationManager::Get().AddNotification(Info);
	}
	else if (ImportedCount > 0)
	{
		FNotificationInfo Info(FText::Format(NSLOCTEXT("SimpleQuestEditor", "ImportedOutcomes",
			"Imported {0} outcome pin(s)"),
			FText::AsNumber(ImportedCount)));
		Info.ExpireDuration = 3.0f;
		FSlateNotificationManager::Get().AddNotification(Info);
	}
	else if (StaleIndices.Num() > 0)
	{
		FNotificationInfo Info(FText::Format(NSLOCTEXT("SimpleQuestEditor", "PrunedOutcomes",
			"Flagged {0} stale pin(s) for cleanup; no new outcomes to import"),
			FText::AsNumber(StaleIndices.Num())));
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

