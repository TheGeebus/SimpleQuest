// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Nodes/QuestlineNode_Entry.h"
#include "Nodes/QuestlineNode_LinkedQuestline.h"
#include "Quests/QuestlineGraph.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Utilities/SimpleQuestEditorUtils.h"
#include "Utilities/QuestlineGraphTraversalPolicy.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Nodes/QuestlineNode_ContentBase.h"
#include "SimpleQuestLog.h"
#include "Utilities/QuestlineGraphTraversalPolicy.h"


/*------------------------------------------------------------------------------------------------*
 * Helpers
 *------------------------------------------------------------------------------------------------*/

/**
 * Builds the disambiguated display/pin name for an exposed spec, given all exposed specs on the same Entry node.
 * Graded: bare outcome name when unique; source-qualified when the outcome appears on multiple specs; cross-asset-qualified
 * when the source label also collides across different parent assets. The returned name is used BOTH as the internal
 * FName for the pin and as its human-readable label — runtime routing uses the FName form.
 */
FName UQuestlineNode_Entry::BuildDisambiguatedPinName(const FIncomingSignalPinSpec& Spec, const TArray<FIncomingSignalPinSpec>& AllSpecs)
{
	const FName OutcomeName = Spec.Outcome.GetTagName();
	const FString OutcomeLeaf = [&OutcomeName]()
	{
		const FString Full = OutcomeName.ToString();
		int32 LastDot; if (Full.FindLastChar(TEXT('.'), LastDot)) return Full.Mid(LastDot + 1); return Full;
	}();

	// Collect sibling exposed specs sharing the same outcome tag (excluding ourselves).
	TArray<const FIncomingSignalPinSpec*> Siblings;
	for (const FIncomingSignalPinSpec& Other : AllSpecs)
	{
		if (&Other == &Spec) continue;
		if (!Other.bExposed) continue;
		if (Other.Outcome != Spec.Outcome) continue;
		Siblings.Add(&Other);
	}

	if (Siblings.Num() == 0) return FName(*OutcomeLeaf);

	// Outcome collides with siblings — qualify by source label.
	const FString SourceLabel = Spec.CachedSourceLabel.IsEmpty() ? TEXT("Unknown") : Spec.CachedSourceLabel;
	const FString Qualified = FString::Printf(TEXT("%s (%s)"), *OutcomeLeaf, *SourceLabel);

	// Check whether source-qualified names still collide across the sibling set (different assets, same source label).
	bool bSourceLabelCollides = false;
	for (const FIncomingSignalPinSpec* Sibling : Siblings)
	{
		const FString SiblingLabel = Sibling->CachedSourceLabel.IsEmpty() ? TEXT("Unknown") : Sibling->CachedSourceLabel;
		if (SiblingLabel == SourceLabel) { bSourceLabelCollides = true; break; }
	}

	if (!bSourceLabelCollides) return FName(*Qualified);

	// Still ambiguous — add asset qualifier. Use asset short name; Session 22 can upgrade to FriendlyName when that lands.
	const FString AssetLabel = Spec.ParentAsset.IsNull() ? TEXT("Local") : Spec.ParentAsset.GetAssetName();
	const FString DeepQualified = FString::Printf(TEXT("%s (%s / %s)"), *OutcomeLeaf, *SourceLabel, *AssetLabel);
	return FName(*DeepQualified);
}

/*------------------------------------------------------------------------------------------------*
 * End helper section
 *------------------------------------------------------------------------------------------------*/

void UQuestlineNode_Entry::AllocateDefaultPins()
{
	/**
	 * One pin per exposed spec — not per outcome tag. Multiple specs may share an outcome tag when different sources
	 * produce it; each gets its own pin with a disambiguated name so downstream wiring can be source-specific. Unexposed
	 * specs are stored but generate no pin (they persist the designer's selection state across re-imports).
	 */
	for (const FIncomingSignalPinSpec& Spec : IncomingSignals)
	{
		if (!Spec.bExposed) continue;
		if (!Spec.Outcome.IsValid()) continue;
		CreatePin(EGPD_Output, TEXT("QuestOutcome"), BuildDisambiguatedPinName(Spec, IncomingSignals));
	}

	// Unconditional path — always fires when this graph activates, regardless of which outcome (or source) triggered entry.
	CreatePin(EGPD_Output, TEXT("QuestActivation"), TEXT("Any Outcome"));

	// Deactivation — fires when the parent Quest node is deactivated.
	if (bShowDeactivationPins)
	{
		CreatePin(EGPD_Output, TEXT("QuestDeactivated"), TEXT("Deactivated"));
	}
}

void UQuestlineNode_Entry::PostLoad()
{
	Super::PostLoad();

	/**
	 * Scrub unqualified specs — invalid state in the post-legacy-removal model. Only present on assets authored during
	 * Session 19 or earlier; a warning surfaces them so the designer knows to re-run Import.
	 */
	int32 RemovedCount = 0;
	for (int32 i = IncomingSignals.Num() - 1; i >= 0; --i)
	{
		if (!IncomingSignals[i].SourceNodeGuid.IsValid()) { IncomingSignals.RemoveAt(i); ++RemovedCount; }
	}

	if (RemovedCount > 0)
	{
		UE_LOG(LogSimpleQuest, Warning,
			TEXT("UQuestlineNode_Entry::PostLoad: scrubbed %d unqualified incoming-signal spec(s) on %s. Re-run Import to populate source-qualified specs."),
			RemovedCount, *GetName());
	}

	RefreshOutcomePins();
}

void UQuestlineNode_Entry::RefreshOutcomePins()
{
	TArray<FName> DesiredNames;
	for (const FIncomingSignalPinSpec& Spec : IncomingSignals)
	{
		if (!Spec.bExposed) continue;
		if (!Spec.Outcome.IsValid()) continue;
		DesiredNames.Add(BuildDisambiguatedPinName(Spec, IncomingSignals));
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
	if (!GetGraph()) return;

	/**
	 * Determine our own asset so we can mark cross-asset sources. ParentAsset on a spec stays empty when the source
	 * pin lives in the same asset as this Entry.
	 */
	const UQuestlineGraph* OwnAsset = FQuestlineGraphTraversalPolicy::ResolveContainingAsset(GetGraph());

	// Walk parent assets for outcome pins routing into this Entry.
	FQuestlineGraphTraversalPolicy TraversalPolicy;
	TSet<FQuestEffectiveSource> ReachingSources;
	TraversalPolicy.CollectEntryReachingSources(GetGraph(), ReachingSources);

	// Each routed outcome pin maps to one qualified target — the data we want reflected in a spec.
	struct FQualifiedTarget
	{
		FGuid SourceNodeGuid;
		FGameplayTag Outcome;
		FSoftObjectPath ParentAsset;
		FString SourceLabel;

		bool MatchesIdentity(const FIncomingSignalPinSpec& Spec) const
		{
			return Spec.SourceNodeGuid == SourceNodeGuid && Spec.Outcome == Outcome && Spec.ParentAsset == ParentAsset;
		}
	};

	TArray<FQualifiedTarget> Targets;
	for (const FQuestEffectiveSource& Source : ReachingSources)
	{
		if (!Source.Pin) continue;
		// Named outcome pins only — AnyOutcome routes lose per-outcome identity at the boundary.
		if (Source.Pin->PinType.PinCategory != TEXT("QuestOutcome")) continue;
		if (Source.Pin->PinName.IsNone()) continue;

		const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(Source.Pin->PinName, /*bErrorIfNotFound=*/ false);
		if (!Tag.IsValid()) continue;

		const UQuestlineNode_ContentBase* ContentNode = Cast<UQuestlineNode_ContentBase>(Source.Pin->GetOwningNode());
		if (!ContentNode) continue;

		const FGuid SourceGuid = ContentNode->QuestGuid;
		if (!SourceGuid.IsValid()) continue;

		FQualifiedTarget Target;
		Target.SourceNodeGuid = SourceGuid;
		Target.Outcome = Tag;
		Target.ParentAsset = (Source.Asset && Source.Asset != OwnAsset) ? FSoftObjectPath(Source.Asset) : FSoftObjectPath();
		Target.SourceLabel = ContentNode->NodeLabel.ToString();
		Targets.Add(Target);
	}

	// Decide per-target action: re-enable existing, refresh label on existing, or append new.
	TArray<int32> IndicesToReEnable;
	TArray<int32> IndicesToRefreshLabel;
	TArray<FQualifiedTarget> SpecsToAdd;
	for (const FQualifiedTarget& Target : Targets)
	{
		int32 MatchIndex = INDEX_NONE;
		for (int32 i = 0; i < IncomingSignals.Num(); ++i)
		{
			if (Target.MatchesIdentity(IncomingSignals[i])) { MatchIndex = i; break; }
		}

		if (MatchIndex == INDEX_NONE) { SpecsToAdd.Add(Target); continue; }

		if (!IncomingSignals[MatchIndex].bExposed) IndicesToReEnable.Add(MatchIndex);
		if (IncomingSignals[MatchIndex].CachedSourceLabel != Target.SourceLabel) IndicesToRefreshLabel.Add(MatchIndex);
	}

	// Stale pruning: exposed specs whose identity triple no longer appears in Targets.
	TArray<int32> StaleIndices;
	for (int32 i = 0; i < IncomingSignals.Num(); ++i)
	{
		const FIncomingSignalPinSpec& Spec = IncomingSignals[i];
		if (!Spec.bExposed) continue;
		if (!Targets.ContainsByPredicate([&Spec](const FQualifiedTarget& T) { return T.MatchesIdentity(Spec); })) StaleIndices.Add(i);
	}

	const bool bWouldImport       = SpecsToAdd.Num() > 0 || IndicesToReEnable.Num() > 0;
	const bool bWouldPrune        = StaleIndices.Num() > 0;
	const bool bWouldRefreshLabel = IndicesToRefreshLabel.Num() > 0;
	if (!bWouldImport && !bWouldPrune && !bWouldRefreshLabel)
	{
		FNotificationInfo Info(NSLOCTEXT("SimpleQuestEditor", "NoParentOutcomes",
			"No outcome tags are routed into this graph's parent Activate pin"));
		Info.ExpireDuration = 3.0f;
		FSlateNotificationManager::Get().AddNotification(Info);
		return;
	}

	const FScopedTransaction Transaction(NSLOCTEXT("SimpleQuestEditor", "ImportOutcomePins_Undo", "Import Outcome Pins"));
	Modify();

	for (int32 i = StaleIndices.Num() - 1; i >= 0; --i) IncomingSignals[StaleIndices[i]].bExposed = false;
	for (int32 Index : IndicesToReEnable) IncomingSignals[Index].bExposed = true;
	for (int32 Index : IndicesToRefreshLabel)
	{
		// Refresh label from the matching target.
		for (const FQualifiedTarget& Target : Targets)
		{
			if (Target.MatchesIdentity(IncomingSignals[Index])) { IncomingSignals[Index].CachedSourceLabel = Target.SourceLabel; break; }
		}
	}
	for (const FQualifiedTarget& Target : SpecsToAdd)
	{
		FIncomingSignalPinSpec Spec;
		Spec.SourceNodeGuid = Target.SourceNodeGuid;
		Spec.Outcome = Target.Outcome;
		Spec.ParentAsset = Target.ParentAsset;
		Spec.CachedSourceLabel = Target.SourceLabel;
		Spec.bExposed = true;
		IncomingSignals.Add(Spec);
	}

	const int32 ImportedCount = SpecsToAdd.Num() + IndicesToReEnable.Num();

	if (ImportedCount > 0 || StaleIndices.Num() > 0 || IndicesToRefreshLabel.Num() > 0) RefreshOutcomePins();

	// Four-way toast matrix. Label-refresh alone falls through to a dedicated message.
	if (ImportedCount > 0 && StaleIndices.Num() > 0)
	{
		FNotificationInfo Info(FText::Format(
			NSLOCTEXT("SimpleQuestEditor", "ImportedAndPrunedOutcomes", "Imported {0} outcome pin(s); flagged {1} stale pin(s) for cleanup"),
			FText::AsNumber(ImportedCount), FText::AsNumber(StaleIndices.Num())));
		Info.ExpireDuration = 3.5f;
		FSlateNotificationManager::Get().AddNotification(Info);
	}
	else if (ImportedCount > 0)
	{
		FNotificationInfo Info(FText::Format(
			NSLOCTEXT("SimpleQuestEditor", "ImportedOutcomes", "Imported {0} outcome pin(s)"),
			FText::AsNumber(ImportedCount)));
		Info.ExpireDuration = 3.0f;
		FSlateNotificationManager::Get().AddNotification(Info);
	}
	else if (StaleIndices.Num() > 0)
	{
		FNotificationInfo Info(FText::Format(
			NSLOCTEXT("SimpleQuestEditor", "PrunedOutcomes", "Flagged {0} stale pin(s) for cleanup; no new outcomes to import"),
			FText::AsNumber(StaleIndices.Num())));
		Info.ExpireDuration = 3.0f;
		FSlateNotificationManager::Get().AddNotification(Info);
	}
	else if (IndicesToRefreshLabel.Num() > 0)
	{
		FNotificationInfo Info(FText::Format(
			NSLOCTEXT("SimpleQuestEditor", "RefreshedSourceLabels", "Refreshed source labels for {0} spec(s)"),
			FText::AsNumber(IndicesToRefreshLabel.Num())));
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

