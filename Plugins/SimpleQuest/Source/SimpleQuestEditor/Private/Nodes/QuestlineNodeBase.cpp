// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Nodes/QuestlineNodeBase.h"

#include "Types/QuestPinRole.h"
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

EQuestPinRole UQuestlineNodeBase::GetPinRole(const UEdGraphPin* Pin) const
{
	if (!Pin) return EQuestPinRole::None;

	const FName PinName = Pin->PinName;
	const FName Cat     = Pin->PinType.PinCategory;

	if (Pin->Direction == EGPD_Input)
	{
		if ((PinName == TEXT("Activate") || PinName == TEXT("Enter")) && Cat == TEXT("QuestActivation"))
		{
			return EQuestPinRole::ExecIn;
		}
		if (PinName == TEXT("Deactivate") && Cat == TEXT("QuestDeactivate"))
		{
			return EQuestPinRole::DeactivateIn;
		}
		if (PinName == TEXT("Enter") && Cat == TEXT("QuestPrerequisite"))
		{
			return EQuestPinRole::PrereqIn;
		}
		if (Cat == TEXT("QuestPrerequisite") && PinName.ToString().StartsWith(TEXT("Condition_")))
		{
			return EQuestPinRole::PrereqIn;
		}
	}
	else if (Pin->Direction == EGPD_Output)
	{
		if ((PinName == TEXT("Forward") || PinName == TEXT("Exit"))	&& Cat == TEXT("QuestActivation"))
		{
			return EQuestPinRole::ExecForwardOut;
		}
		if ((PinName == TEXT("Any Outcome") || PinName == TEXT("Entered")) && Cat == TEXT("QuestActivation"))
		{
			return EQuestPinRole::AnyOutcomeOut;
		}
		if (Cat == TEXT("QuestOutcome"))
		{
			return EQuestPinRole::NamedOutcomeOut;
		}
		if (Cat == TEXT("QuestDeactivated"))
		{
			return EQuestPinRole::DeactivatedOut;
		}
		if ((PinName == TEXT("PrereqOut") || PinName == TEXT("Forward") || PinName == TEXT("Exit")) && Cat == TEXT("QuestPrerequisite"))
		{
			return EQuestPinRole::PrereqOut;
		}
	}

	return EQuestPinRole::None;
}

UEdGraphPin* UQuestlineNodeBase::GetPinByRole(EQuestPinRole Role) const
{
	for (UEdGraphPin* Pin : Pins)
	{
		if (GetPinRole(Pin) == Role) return Pin;
	}
	return nullptr;
}

EQuestPinRole UQuestlineNodeBase::GetPinRoleOf(const UEdGraphPin* Pin)
{
	if (!Pin) return EQuestPinRole::None;
	const UQuestlineNodeBase* Base = Cast<const UQuestlineNodeBase>(Pin->GetOwningNode());
	return Base ? Base->GetPinRole(Pin) : EQuestPinRole::None;
}

UEdGraphPin* UQuestlineNodeBase::FindPinByRole(const UEdGraphNode* Node, EQuestPinRole Role)
{
	if (!Node) return nullptr;
	const UQuestlineNodeBase* Base = Cast<const UQuestlineNodeBase>(Node);
	return Base ? Base->GetPinByRole(Role) : nullptr;
}

void UQuestlineNodeBase::GetPinsByRole(EQuestPinRole Role, TArray<UEdGraphPin*>& OutPins) const
{
	for (UEdGraphPin* Pin : Pins)
	{
		if (GetPinRole(Pin) == Role) OutPins.Add(Pin);
	}
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

    // Deactivation-flavored drag source: ensure deactivation pins exist before the role query —
    // they default off and would otherwise be invisible to the walker (same rationale as before,
    // now expressed via category since FromPin belongs to a different node).
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

    // Classify this node's candidate-direction pins into role buckets via the virtual role query.
    TArray<UEdGraphPin*> ExecIns, PrereqIns, DeactivateIns, OtherPins;
    TArray<UEdGraphPin*> ExecForwardOuts, AnyOutcomeOuts, NamedOutcomeOuts, DeactivatedOuts, PrereqOuts;

    for (UEdGraphPin* Pin : Pins)
    {
        if (!Pin || Pin->bHidden) continue;
        if (Pin->Direction != DesiredDirection) continue;

        switch (GetPinRole(Pin))
        {
            case EQuestPinRole::ExecIn:            ExecIns.Add(Pin);          break;
            case EQuestPinRole::PrereqIn:          PrereqIns.Add(Pin);        break;
            case EQuestPinRole::DeactivateIn:      DeactivateIns.Add(Pin);    break;
            case EQuestPinRole::ExecForwardOut:    ExecForwardOuts.Add(Pin);  break;
            case EQuestPinRole::AnyOutcomeOut:     AnyOutcomeOuts.Add(Pin);   break;
            case EQuestPinRole::NamedOutcomeOut:   NamedOutcomeOuts.Add(Pin); break;
            case EQuestPinRole::DeactivatedOut:    DeactivatedOuts.Add(Pin);  break;
            case EQuestPinRole::PrereqOut:         PrereqOuts.Add(Pin);       break;
            default:                               OtherPins.Add(Pin);        break;
        }
    }

    // Among multi-slot prereq inputs (combinator Condition_N), prefer empty slots — the single-wire
    // rule on prereq inputs rejects any new connection to an already-wired pin. Single-slot cases
    // (content's Prerequisites, future Prereq Rule Entry's Enter) sort trivially.
    PrereqIns.Sort([](const UEdGraphPin& A, const UEdGraphPin& B)
    {
        const bool bAEmpty = A.LinkedTo.Num() == 0;
        const bool bBEmpty = B.LinkedTo.Num() == 0;
        if (bAEmpty != bBEmpty) return bAEmpty;
        return A.PinName.LexicalLess(B.PinName);
    });

    // Priority-ordered candidate list by FromPin's category — matches natural semantic pairing.
    TArray<UEdGraphPin*> Candidates;

    if (DesiredDirection == EGPD_Input)
    {
        if (FromCat == TEXT("QuestDeactivated"))
        {
            // Deactivation cascades naturally into Deactivate; Activate is a reasonable fallback.
            Candidates.Append(DeactivateIns);
            Candidates.Append(ExecIns);
            Candidates.Append(PrereqIns);
        }
        else if (FromCat == TEXT("QuestPrerequisite"))
        {
            // Prereq output chains into Prerequisites or another combinator Condition pin.
            Candidates.Append(PrereqIns);
            Candidates.Append(ExecIns);
            Candidates.Append(DeactivateIns);
        }
        else
        {
            // QuestOutcome / QuestActivation — the common case. Activation first.
            Candidates.Append(ExecIns);
            Candidates.Append(PrereqIns);
            Candidates.Append(DeactivateIns);
        }
    }
    else
    {
        if (FromCat == TEXT("QuestDeactivate"))
        {
            // Back-drag from a Deactivate input wants a Deactivated output on the new node.
            Candidates.Append(DeactivatedOuts);
            Candidates.Append(ExecForwardOuts);
            Candidates.Append(AnyOutcomeOuts);
            Candidates.Append(NamedOutcomeOuts);
            Candidates.Append(PrereqOuts);
        }
        else if (FromCat == TEXT("QuestPrerequisite"))
        {
            Candidates.Append(PrereqOuts);
            Candidates.Append(NamedOutcomeOuts);
            Candidates.Append(AnyOutcomeOuts);
            Candidates.Append(ExecForwardOuts);
            Candidates.Append(DeactivatedOuts);
        }
        else
        {
            Candidates.Append(ExecForwardOuts);
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
	FSimpleQuestEditorUtilities::SyncPinsByCategory(this, Direction, PinCategory, DesiredPinNames, InsertBeforeCategories);
}

