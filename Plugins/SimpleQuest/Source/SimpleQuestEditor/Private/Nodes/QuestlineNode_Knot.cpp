// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Nodes/QuestlineNode_Knot.h"

#include "Graph/QuestlineGraphSchema.h"
#include "Types/QuestPinRole.h"


// QuestlineNode_Knot.cpp
void UQuestlineNode_Knot::AllocateDefaultPins()
{
	CreatePin(EGPD_Input,  TEXT("QuestActivation"), TEXT("KnotIn"));
	CreatePin(EGPD_Output, TEXT("QuestActivation"), TEXT("KnotOut"));
}

EQuestPinRole UQuestlineNode_Knot::GetPinRole(const UEdGraphPin* Pin) const
{
	if (!Pin) return EQuestPinRole::None;
	if (Pin->PinName == TEXT("KnotIn"))  return EQuestPinRole::ExecIn;
	if (Pin->PinName == TEXT("KnotOut")) return EQuestPinRole::ExecForwardOut;
	return EQuestPinRole::None;
}

void UQuestlineNode_Knot::AutowireNewNode(UEdGraphPin* FromPin)
{
	if (!FromPin) return;

	const UEdGraphSchema* Schema = GetSchema();

	// Inherit the category from the source pin so wire color flows through
	UEdGraphPin* KnotIn  = FindPin(TEXT("KnotIn"));
	UEdGraphPin* KnotOut = FindPin(TEXT("KnotOut"));

	if (KnotIn)  KnotIn->PinType  = FromPin->PinType;
	if (KnotOut) KnotOut->PinType = FromPin->PinType;

	// Make connections
	if (FromPin->Direction == EGPD_Output)
	{
		if (Schema->TryCreateConnection(FromPin, KnotIn))
		{
			FromPin->GetOwningNode()->NodeConnectionListChanged();
		}
	}
	else if (FromPin->Direction == EGPD_Input)
	{
		if (Schema->TryCreateConnection(KnotOut, FromPin))
		{
			FromPin->GetOwningNode()->NodeConnectionListChanged();
		}
	}
}

FName UQuestlineNode_Knot::GetEffectiveCategory() const
{
	const UEdGraphPin* InPin  = FindPin(TEXT("KnotIn"));
	const UEdGraphPin* OutPin = FindPin(TEXT("KnotOut"));

	// WHERE A WIRE ENDS DECIDES HOW IT LOOKS, not where it starts. An activation or outcome wire plugged into a
	// Prerequisites pin draws dashed and pink for its whole length, so a reroute sitting on it has to be colored
	// like the wire rather than like its source. This asks the drawing policy's own question rather than a
	// lookalike, so the dot and the line can never disagree.
	if (InPin)
	{
		TSet<const UEdGraphNode*> Visited;
		if (UQuestlineGraphSchema::LeadsOnlyToPrereqInputs(InPin, Visited))
		{
			return TEXT("QuestPrerequisite");
		}
	}

	if (InPin && InPin->LinkedTo.Num() > 0)
		return InPin->LinkedTo[0]->PinType.PinCategory; // let input determine signal type (success, fail, either)

	if (OutPin && OutPin->LinkedTo.Num() > 0)
		return OutPin->LinkedTo[0]->PinType.PinCategory; // fallback to output type if input is unset

	return TEXT("QuestActivation");
}

void UQuestlineNode_Knot::NodeConnectionListChanged()
{
	Super::NodeConnectionListChanged();
	SyncPinCategoryFromLinks();
}

void UQuestlineNode_Knot::PinConnectionListChanged(UEdGraphPin* Pin)
{
	Super::PinConnectionListChanged(Pin);

	// THIS is the hook the engine actually calls when a wire is made: UEdGraphSchema::TryCreateConnection ends in
	// PinConnectionListChanged(Pin) on each side and never calls NodeConnectionListChanged(). Without this override
	// the retype below only ran when something invoked that method by hand, so a knot dropped with R and then wired
	// up kept the QuestActivation category it was created with - a white activation dot sitting on a purple dashed
	// prerequisite wire. The two creation paths that looked correct were correct for a different reason: both
	// AutowireNewNode and the schema's reroute insertion assign PinType directly and never needed the hook.
	// NodeConnectionListChanged stays overridden because bulk edits - paste, delete fixups - go through it instead.
	SyncPinCategoryFromLinks();
}

void UQuestlineNode_Knot::SyncPinCategoryFromLinks()
{
	UEdGraphPin* InPin  = FindPin(TEXT("KnotIn"));
	UEdGraphPin* OutPin = FindPin(TEXT("KnotOut"));
	if (!InPin || !OutPin) return;

	const FName NewCategory = GetEffectiveCategory();
	if (InPin->PinType.PinCategory == NewCategory && OutPin->PinType.PinCategory == NewCategory)
		return;

	Modify();
	InPin->PinType.PinCategory  = NewCategory;
	OutPin->PinType.PinCategory = NewCategory;

	// Nothing notifies a node when a NEIGHBOR's pin type changes - only when a connection does - so a retype has to
	// be pushed along the chain by hand. Both directions matter, because the two rules in GetEffectiveCategory look
	// opposite ways: the prereq test reads DOWNSTREAM, so knots UPSTREAM of us must re-ask now that our answer
	// changed; the source-category fallback reads UPSTREAM, so knots downstream must re-ask too. The early-out
	// above terminates both - a knot that already matches returns without walking on, so a loop settles.
	for (UEdGraphPin* Linked : InPin->LinkedTo)
	{
		if (!Linked) continue;
		if (UQuestlineNode_Knot* Upstream = Cast<UQuestlineNode_Knot>(Linked->GetOwningNode()))
		{
			Upstream->SyncPinCategoryFromLinks();
		}
	}
	for (UEdGraphPin* Linked : OutPin->LinkedTo)
	{
		if (!Linked) continue;
		if (UQuestlineNode_Knot* Downstream = Cast<UQuestlineNode_Knot>(Linked->GetOwningNode()))
		{
			Downstream->SyncPinCategoryFromLinks();
		}
	}
}

