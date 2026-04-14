// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Nodes/QuestlineNodeBase.h"

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

void UQuestlineNodeBase::SyncPinsByCategory(EEdGraphPinDirection Direction, FName PinCategory, const TArray<FName>& DesiredPinNames, const TSet<FName>& InsertBeforeCategories)
{
	// ----- Collect existing pins of this category (including orphaned) -----

	TArray<UEdGraphPin*> ExistingPins;
	for (UEdGraphPin* Pin : Pins)
	{
		if (Pin->Direction == Direction && Pin->PinType.PinCategory == PinCategory)
		{
			ExistingPins.Add(Pin);
		}
	}

	// ----- Diff against desired set -----

	bool bChanged = false;

	// Orphan pins that are no longer desired; un-orphan pins that are desired again
	TArray<UEdGraphPin*> PinsToRemove;
	for (UEdGraphPin* Pin : ExistingPins)
	{
		const bool bDesired = DesiredPinNames.Contains(Pin->PinName);
		if (bDesired && Pin->bOrphanedPin)
		{
			// Pin was stale but is desired again — restore it
			Pin->bOrphanedPin = false;
			bChanged = true;
		}
		else if (!bDesired && !Pin->bOrphanedPin)
		{
			if (Pin->LinkedTo.Num() > 0)
			{
				// Pin has wires — mark stale so designer can re-route
				Pin->bOrphanedPin = true;
			}
			else
			{
				// No connections — nothing to preserve, remove immediately
				PinsToRemove.Add(Pin);
			}
			bChanged = true;
		}
	}

	// Find truly new names: not represented by any existing pin (active or orphaned)
	TArray<FName> NamesToAdd;
	for (const FName& Name : DesiredPinNames)
	{
		const bool bExists = ExistingPins.ContainsByPredicate(
			[&](const UEdGraphPin* Pin) { return Pin->PinName == Name; });
		if (!bExists) NamesToAdd.Add(Name);
	}

	if (!bChanged && NamesToAdd.IsEmpty()) return; // No changes detected

	// ----- Apply changes -----

	Modify();

	// Get rid of only undesired pins with no connections
	for (UEdGraphPin* Pin : PinsToRemove)
	{
		RemovePin(Pin);
	}
	
	// Find insertion point
	int32 InsertIndex = INDEX_NONE;
	if (!InsertBeforeCategories.IsEmpty())
	{
		for (int32 i = 0; i < Pins.Num(); i++)
		{
			if (InsertBeforeCategories.Contains(Pins[i]->PinType.PinCategory))
			{
				InsertIndex = i;
				break;
			}
		}
	}

	FEdGraphPinType PinType;
	PinType.PinCategory = PinCategory;

	for (const FName& Name : NamesToAdd)
	{
		CreatePin(Direction, PinType, Name, InsertIndex);
		if (InsertIndex != INDEX_NONE) ++InsertIndex;
	}

	// Notify listeners
	if (UEdGraph* Graph = GetGraph())
	{
		Graph->NotifyGraphChanged();
	}
}

