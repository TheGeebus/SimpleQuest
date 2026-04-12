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

void UQuestlineNodeBase::SyncPinsByCategory(EEdGraphPinDirection Direction, FName PinCategory, const TArray<FName>& DesiredPinNames, const TSet<FName>& InsertBeforeCategories)
{
	// ----- Diff existing and desired pins -----
	
	// Collect existing pins matching category and direction
	TArray<UEdGraphPin*> ExistingPins;
	for (UEdGraphPin* Pin : Pins)
	{
		if (Pin->Direction == Direction && Pin->PinType.PinCategory == PinCategory)	ExistingPins.Add(Pin);
	}

	// Find the stale pins to remove: Existing pin is not in Desired Pins 
	TArray<UEdGraphPin*> PinsToRemove;
	for (UEdGraphPin* Pin : ExistingPins)
	{
		if (!DesiredPinNames.Contains(Pin->PinName)) PinsToRemove.Add(Pin);
	}

	// Find the new pins to add: Desired pin is not in Existing Pins
	TArray<FName> NamesToAdd;
	for (const FName& Name : DesiredPinNames)
	{
		const bool bExists = ExistingPins.ContainsByPredicate([&](const UEdGraphPin* Pin) { return Pin->PinName == Name; }); // exact name match
		if (!bExists) NamesToAdd.Add(Name);
	}

	if (PinsToRemove.IsEmpty() && NamesToAdd.IsEmpty()) return;	// Nothing changed, no reason to rebuild, return early

	// ----- Make the new pins -----
	
	Modify();

	for (UEdGraphPin* Pin : PinsToRemove)
	{
		Pin->BreakAllPinLinks(false);
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

	// Make pin
	FEdGraphPinType PinType;
	PinType.PinCategory = PinCategory;

	for (const FName& Name : NamesToAdd)
	{
		CreatePin(Direction, PinType, Name, InsertIndex);
		if (InsertIndex != INDEX_NONE) ++InsertIndex;
	}

	// Notify listeners that the graph changed
	if (UEdGraph* Graph = GetGraph())
	{
		Graph->NotifyGraphChanged();
	}
}
