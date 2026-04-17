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

void UQuestlineNodeBase::AutowireNewNode(UEdGraphPin* FromPin)
{
    if (!FromPin) return;
    const UEdGraphSchema* Schema = GetSchema();
    if (!Schema) return;

	// If the drag source is a deactivation-flavored pin, ensure this node's deactivation pins are allocated before the candidate
	// walk — they default off and would otherwise not be findable, causing the walker to fall through to Activate.
	const FName FromCat = FromPin->PinType.PinCategory;
	const bool bDragSourceIsDeactivation =
		   FromCat == TEXT("QuestDeactivated")
		|| FromCat == TEXT("QuestDeactivate");
	if (bDragSourceIsDeactivation)
	{
		EnsureDeactivationPinsForAutowire();
	}
	
    const EEdGraphPinDirection DesiredDirection =
        (FromPin->Direction == EGPD_Output) ? EGPD_Input : EGPD_Output;

    // Bucket pins by role so we can order them by natural semantic preference.
    TArray<UEdGraphPin*> ActivateIns;
    TArray<UEdGraphPin*> ConditionIns;
    TArray<UEdGraphPin*> PrereqIns;
    TArray<UEdGraphPin*> DeactivateIns;
    TArray<UEdGraphPin*> ForwardOuts;
    TArray<UEdGraphPin*> AnyOutcomeOuts;
    TArray<UEdGraphPin*> NamedOutcomeOuts;
	TArray<UEdGraphPin*> DeactivatedOuts;
    TArray<UEdGraphPin*> PrereqOuts;
    TArray<UEdGraphPin*> OtherPins;

    for (UEdGraphPin* Pin : Pins)
    {
        if (!Pin || Pin->bHidden) continue;
        if (Pin->Direction != DesiredDirection) continue;

        const FName PinName = Pin->PinName;
        const FName Cat = Pin->PinType.PinCategory;

        if (DesiredDirection == EGPD_Input)
        {
	        if (PinName == TEXT("Activate") && Cat == TEXT("QuestActivation"))
	        {
	        	ActivateIns.Add(Pin);
	        }
	        else if (Cat == TEXT("QuestPrerequisite") && PinName.ToString().StartsWith(TEXT("Condition_")))
	        {
	        	ConditionIns.Add(Pin);
	        }
	        else if (PinName == TEXT("Prerequisites") && Cat == TEXT("QuestPrerequisite"))
	        {
	        	PrereqIns.Add(Pin);
	        }
	        else if (PinName == TEXT("Deactivate") && Cat == TEXT("QuestDeactivate"))
	        {
	        	DeactivateIns.Add(Pin);
	        }
	        else
	        {
	        	OtherPins.Add(Pin);
	        }
        }
        else  // EGPD_Output
        {
	        if (PinName == TEXT("Forward") && Cat == TEXT("QuestActivation"))
	        {
	        	ForwardOuts.Add(Pin);
	        }
	        else if (PinName == TEXT("Any Outcome") && Cat == TEXT("QuestActivation"))
	        {
	        	AnyOutcomeOuts.Add(Pin);
	        }
	        else if (Cat == TEXT("QuestOutcome"))
	        {
	        	NamedOutcomeOuts.Add(Pin);
	        }
	        else if (Cat == TEXT("QuestDeactivated"))
	        {
		        DeactivatedOuts.Add(Pin);
	        }
        	else if (PinName == TEXT("PrereqOut") && Cat == TEXT("QuestPrerequisite"))
	        {
	        	PrereqOuts.Add(Pin);
	        }
	        else
	        {
	        	OtherPins.Add(Pin);
	        }
        }
    }

    // Among Condition_N pins, prefer empty ones first — the single-wire rule on prereq inputs
    // rejects any new connection to an already-wired pin.
    ConditionIns.Sort([](const UEdGraphPin& A, const UEdGraphPin& B)
    {
        const bool bAEmpty = A.LinkedTo.Num() == 0;
        const bool bBEmpty = B.LinkedTo.Num() == 0;
        if (bAEmpty != bBEmpty)
        {
	        return bAEmpty;
        }
        return A.PinName.LexicalLess(B.PinName);
    });

    // Build the priority-ordered candidate list by FromPin's category.
    TArray<UEdGraphPin*> Candidates;

    if (DesiredDirection == EGPD_Input)
    {
    	if (FromCat == TEXT("QuestDeactivated"))
    	{
    		// Deactivation cascades naturally into Deactivate; Activate is a reasonable fallback.
    		Candidates.Append(DeactivateIns);
    		Candidates.Append(ActivateIns);
    		Candidates.Append(ConditionIns);
    		Candidates.Append(PrereqIns);
    	}
        else if (FromCat == TEXT("QuestPrerequisite"))
        {
            // Prereq output chains into Prerequisites or another combinator Condition pin.
            Candidates.Append(PrereqIns);
            Candidates.Append(ConditionIns);
            Candidates.Append(ActivateIns);
            Candidates.Append(DeactivateIns);
        }
        else
        {
            // QuestOutcome / QuestActivation — the common case. Activation first.
            Candidates.Append(ActivateIns);
            Candidates.Append(ConditionIns);
            Candidates.Append(PrereqIns);
            Candidates.Append(DeactivateIns);
        }
    }
    else  // Output direction on new node — user dragged from an input
    {
    	if (FromCat == TEXT("QuestDeactivate"))
    	{
    		// Back-drag from a Deactivate input wants a Deactivated output on the new node.
    		Candidates.Append(DeactivatedOuts);
    		Candidates.Append(ForwardOuts);
    		Candidates.Append(AnyOutcomeOuts);
    		Candidates.Append(NamedOutcomeOuts);
    		Candidates.Append(PrereqOuts);
    	}
    	else if (FromCat == TEXT("QuestPrerequisite"))
    	{
    		Candidates.Append(PrereqOuts);
    		Candidates.Append(NamedOutcomeOuts);
    		Candidates.Append(AnyOutcomeOuts);
    		Candidates.Append(ForwardOuts);
    		Candidates.Append(DeactivatedOuts);
    	}
    	else
    	{
    		Candidates.Append(ForwardOuts);
    		Candidates.Append(AnyOutcomeOuts);
    		Candidates.Append(NamedOutcomeOuts);
    		Candidates.Append(PrereqOuts);
    		Candidates.Append(DeactivatedOuts);
    	}
    }
    Candidates.Append(OtherPins);

    // Attempt each candidate; the first one CanCreateConnection accepts wins.
    for (UEdGraphPin* Candidate : Candidates)
    {
        const FPinConnectionResponse R = Schema->CanCreateConnection(FromPin, Candidate);
        const bool bAcceptable =
               R.Response == CONNECT_RESPONSE_MAKE
            || R.Response == CONNECT_RESPONSE_BREAK_OTHERS_A
            || R.Response == CONNECT_RESPONSE_BREAK_OTHERS_B
            || R.Response == CONNECT_RESPONSE_BREAK_OTHERS_AB;
        if (bAcceptable)
        {
            if (Schema->TryCreateConnection(FromPin, Candidate))
            {
                FromPin->GetOwningNode()->NodeConnectionListChanged();
            }
            return;
        }
    }
    // No acceptable candidate — silent no-op. Designer connects manually if needed.
}

void UQuestlineNodeBase::SyncPinsByCategory(EEdGraphPinDirection Direction, FName PinCategory, const TArray<FName>& DesiredPinNames, const TSet<FName>& InsertBeforeCategories)
{
	// Forward call to utility: preserved for backward compatibility
	USimpleQuestEditorUtilities::SyncPinsByCategory(this, Direction, PinCategory, DesiredPinNames, InsertBeforeCategories);
}

