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

    FQuestlineGraphTraversalPolicy TraversalPolicy;

    // Walk back from a container node's Activate input and harvest only the outcome pins that actually feed it.
    // Uses the unified effective-source walker so indirect routes through knots, utility nodes, and activation
    // group setter/getter chains are captured too.
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
                // Only import specific named outcomes. AnyOutcome routes cannot distinguish which outcome fired
                // at runtime, so per-outcome entry pins would be dead; the AnyOutcome entry pin handles that path.
                if (Source->PinType.PinCategory == TEXT("QuestOutcome") && !Source->PinName.IsNone())
                {
                    FoundOutcomeNames.AddUnique(Source->PinName);
                }
            }
        }
    };

    // ── Case 1: inner graph owned by a Quest node ──────────────────────────
    if (const UQuestlineNode_Quest* ParentQuestNode = Cast<UQuestlineNode_Quest>(GraphOuter))
    {
        CollectFromActivatePin(ParentQuestNode);
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

            // For every LinkedQuestline node in the other asset that points at us, harvest the outcomes
            // routed into that node's Activate pin.
            for (const UEdGraphNode* Node : OtherAsset->QuestlineEdGraph->Nodes)
            {
                const UQuestlineNode_LinkedQuestline* LinkedNode =
                    Cast<UQuestlineNode_LinkedQuestline>(Node);
                if (!LinkedNode || LinkedNode->LinkedGraph.ToSoftObjectPath() != OurPath) continue;
                CollectFromActivatePin(LinkedNode);
            }
        }
    }

    // ── Identify stale tags: currently present, no longer routed from any parent ──
    TArray<FGameplayTag> StaleTags;
    for (const FGameplayTag& ExistingTag : IncomingOutcomeTags)
    {
        if (!ExistingTag.IsValid()) continue;
        if (!FoundOutcomeNames.Contains(ExistingTag.GetTagName()))
        {
            StaleTags.Add(ExistingTag);
        }
    }

    // ── Early out when nothing would change ───────────────────────────────
    const bool bWouldImport = !FoundOutcomeNames.IsEmpty();
    const bool bWouldPrune  = !StaleTags.IsEmpty();
    if (!bWouldImport && !bWouldPrune)
    {
        FNotificationInfo Info(NSLOCTEXT("SimpleQuestEditor", "NoParentOutcomes",
            "No outcome tags are routed into this graph's parent Activate pin"));
        Info.ExpireDuration = 3.0f;
        FSlateNotificationManager::Get().AddNotification(Info);
        return;
    }

    // ── Apply additions and pruning under a single undoable transaction ──
    const FScopedTransaction Transaction(NSLOCTEXT("SimpleQuestEditor",
        "ImportOutcomePins_Undo", "Import Outcome Pins"));
    Modify();

    // Prune stale tags first — SyncPinsByCategory (via RefreshOutcomePins) will orphan wired pins
    // whose tags are no longer present, and cleanly remove unwired ones.
    for (const FGameplayTag& Stale : StaleTags)
    {
        IncomingOutcomeTags.Remove(Stale);
    }

    // Import any newly discovered outcomes.
    int32 ImportedCount = 0;
    for (const FName& OutcomeName : FoundOutcomeNames)
    {
        const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(OutcomeName, /*bErrorIfNotFound=*/ false);
        if (!Tag.IsValid()) continue;
        if (IncomingOutcomeTags.Contains(Tag)) continue;

        IncomingOutcomeTags.Add(Tag);
        ImportedCount++;
    }

    // Refresh pins — additions + orphaning of stale wired pins + removal of stale unwired pins.
    if (ImportedCount > 0 || StaleTags.Num() > 0)
    {
        RefreshOutcomePins();
    }

    // ── Summary toast ─────────────────────────────────────────────────────
    if (ImportedCount > 0 && StaleTags.Num() > 0)
    {
        FNotificationInfo Info(FText::Format(NSLOCTEXT("SimpleQuestEditor", "ImportedAndPrunedOutcomes",
			"Imported {0} outcome pin(s); flagged {1} stale pin(s) for cleanup"),
			FText::AsNumber(ImportedCount),
			FText::AsNumber(StaleTags.Num())));
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
    else if (StaleTags.Num() > 0)
    {
        FNotificationInfo Info(FText::Format(NSLOCTEXT("SimpleQuestEditor", "PrunedOutcomes",
        	"Flagged {0} stale pin(s) for cleanup; no new outcomes to import"),
        	FText::AsNumber(StaleTags.Num())));
        Info.ExpireDuration = 3.0f;
        FSlateNotificationManager::Get().AddNotification(Info);
    }
    else
    {
        FNotificationInfo Info(NSLOCTEXT("SimpleQuestEditor", "AllOutcomesPresent",
        	"All routed parent outcomes are already present as incoming pins"));
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

