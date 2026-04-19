// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Nodes/QuestlineNode_Knot.h"

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

	if (InPin && InPin->LinkedTo.Num() > 0)
		return InPin->LinkedTo[0]->PinType.PinCategory; // let input determine signal type (success, fail, either)

	if (OutPin && OutPin->LinkedTo.Num() > 0)
		return OutPin->LinkedTo[0]->PinType.PinCategory; // fallback to output type if input is unset

	return TEXT("QuestActivation");
}

void UQuestlineNode_Knot::NodeConnectionListChanged()
{
	Super::NodeConnectionListChanged();

	UEdGraphPin* InPin  = FindPin(TEXT("KnotIn"));
	UEdGraphPin* OutPin = FindPin(TEXT("KnotOut"));
	if (!InPin || !OutPin) return;

	const FName NewCategory = GetEffectiveCategory();
	if (InPin->PinType.PinCategory == NewCategory && OutPin->PinType.PinCategory == NewCategory)
		return;

	Modify();
	InPin->PinType.PinCategory  = NewCategory;
	OutPin->PinType.PinCategory = NewCategory;
}

