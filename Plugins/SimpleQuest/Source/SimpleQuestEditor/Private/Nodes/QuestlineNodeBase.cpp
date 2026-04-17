// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Nodes/QuestlineNodeBase.h"

#include "Utilities/SimpleQuestEditorUtils.h"

FText UQuestlineNodeBase::GetTagLeafLabel(FName TagName)
{
	const FString Full = TagName.ToString();
	int32 LastDot;
	if (Full.FindLastChar(TEXT('.'), LastDot))
	{
		return FText::FromString(FName::NameToDisplayString(Full.Mid(LastDot + 1), false));
	}
	return FText::FromString(FName::NameToDisplayString(Full, false));
}

FText UQuestlineNodeBase::GetOutcomeLabel(FName TagName)
{
	const FString Full = TagName.ToString();
	int32 OutcomePos = Full.Find(TEXT("Outcome."));
	if (OutcomePos != INDEX_NONE)
	{
		FString Remainder = Full.Mid(OutcomePos + 8);
		TArray<FString> Segments;
		Remainder.ParseIntoArray(Segments, TEXT("."));
		for (FString& Seg : Segments)
		{
			Seg = FName::NameToDisplayString(Seg, false);
		}
		return FText::FromString(FString::Join(Segments, TEXT(": ")));
	}
	return GetTagLeafLabel(TagName);
}

FText UQuestlineNodeBase::GetPinDisplayName(const UEdGraphPin* Pin) const
{
	if (Pin->PinType.PinCategory == TEXT("QuestOutcome"))
	{
		return GetOutcomeLabel(Pin->PinName);
	}
	return Super::GetPinDisplayName(Pin);
}
bool UQuestlineNodeBase::HasStalePins() const
{
	for (const UEdGraphPin* Pin : Pins)
	{
		if (Pin->bOrphanedPin) return true;
	}
	return false;
}

void UQuestlineNodeBase::RemoveStalePins()
{
	TArray<UEdGraphPin*> PinsToRemove;
	for (UEdGraphPin* Pin : Pins)
	{
		if (Pin->bOrphanedPin) PinsToRemove.Add(Pin);
	}

	if (PinsToRemove.IsEmpty()) return;

	const FScopedTransaction Transaction(NSLOCTEXT("SimpleQuestEditor", "RemoveStalePins", "Remove Stale Pins"));
	Modify();

	for (UEdGraphPin* Pin : PinsToRemove)
	{
		Pin->BreakAllPinLinks();
		RemovePin(Pin);
	}

	if (UEdGraph* Graph = GetGraph())
	{
		Graph->NotifyGraphChanged();
	}
}

void UQuestlineNodeBase::GetNodeContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const
{
	Super::GetNodeContextMenuActions(Menu, Context);

	if (HasStalePins())
	{
		FToolMenuSection& Section = Menu->AddSection(TEXT("StalePins"),
			NSLOCTEXT("SimpleQuestEditor", "StalePinsSection", "Stale Pins"));

		Section.AddMenuEntry(
			TEXT("RemoveStalePins"),
			NSLOCTEXT("SimpleQuestEditor", "RemoveStalePins_Label", "Remove Stale Pins"),
			NSLOCTEXT("SimpleQuestEditor", "RemoveStalePins_Tooltip",
				"Break all connections on orphaned pins and remove them from this node"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([NodePtr = const_cast<UQuestlineNodeBase*>(this)]()
			{
				NodePtr->RemoveStalePins();
			}))
		);
	}
}

void UQuestlineNodeBase::PostEditUndo()
{
	Super::PostEditUndo();

	// Many questline nodes mutate their own Pins array inside a transaction (dynamic outcome pins, condition pins, deactivation
	// pins, etc.). The transaction restores the UPROPERTYs and pin objects atomically, but the graph panel's SGraphPin widgets
	// still reference the pre-undo pin pointers. NotifyGraphChanged prompts the panel to reconstruct its pin widgets so they bind
	// to the restored objects. Hoisted to the base class since this is a near-universal hazard across the node hierarchy — any node
	// with dynamic pins needs it.
	if (UEdGraph* Graph = GetGraph())
	{
		Graph->NotifyGraphChanged();
	}
}

void UQuestlineNodeBase::SyncPinsByCategory(EEdGraphPinDirection Direction, FName PinCategory, const TArray<FName>& DesiredPinNames, const TSet<FName>& InsertBeforeCategories)
{
	USimpleQuestEditorUtilities::SyncPinsByCategory(this, Direction, PinCategory, DesiredPinNames, InsertBeforeCategories);
}

