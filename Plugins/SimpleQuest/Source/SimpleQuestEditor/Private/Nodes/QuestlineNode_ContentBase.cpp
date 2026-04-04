#include "Nodes/QuestlineNode_ContentBase.h"

UQuestlineNode_ContentBase::UQuestlineNode_ContentBase()
{
	bCanRenameNode = true;
}

void UQuestlineNode_ContentBase::AllocateDefaultPins()
{
	CreatePin(EGPD_Input,TEXT("QuestActivation"),TEXT("Activate"));
	CreatePin(EGPD_Input,TEXT("QuestActivation"),TEXT("Prerequisites"));
	CreatePin(EGPD_Output,TEXT("QuestSuccess"),TEXT("Success"));
	CreatePin(EGPD_Output,TEXT("QuestFailure"),TEXT("Failure"));
	CreatePin(EGPD_Output,TEXT("QuestActivation"),TEXT("Any Outcome"));
}

void UQuestlineNode_ContentBase::AutowireNewNode(UEdGraphPin* FromPin)
{
	if (!FromPin) return;
	const UEdGraphSchema* Schema = GetSchema();
	if (FromPin->Direction == EGPD_Output)
	{
		if (UEdGraphPin* ActivatePin = FindPin(TEXT("Activate")))
		{
			if (Schema->TryCreateConnection(FromPin, ActivatePin)) FromPin->GetOwningNode()->NodeConnectionListChanged();
		}
	}
	else if (FromPin->Direction == EGPD_Input)
	{
		if (UEdGraphPin* AnyOutcomePin = FindPin(TEXT("Any Outcome")))
		{
			if (Schema->TryCreateConnection(AnyOutcomePin, FromPin)) FromPin->GetOwningNode()->NodeConnectionListChanged();
		}
	}
}

void UQuestlineNode_ContentBase::PostPlacedNewNode()
{
	Super::PostPlacedNewNode();
	QuestGuid = FGuid::NewGuid();
}

void UQuestlineNode_ContentBase::PostDuplicate(bool bDuplicateForPIE)
{
	Super::PostDuplicate(bDuplicateForPIE);
	QuestGuid = FGuid::NewGuid();
}

void UQuestlineNode_ContentBase::OnRenameNode(const FString& NewName)
{
    NodeLabel = FText::FromString(NewName);
}
