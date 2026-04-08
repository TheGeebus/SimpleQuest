#include "Nodes/QuestlineNode_ContentBase.h"

UQuestlineNode_ContentBase::UQuestlineNode_ContentBase()
{
	bCanRenameNode = true;
}

void UQuestlineNode_ContentBase::AllocateDefaultPins()
{
	CreatePin(EGPD_Input,TEXT("QuestActivation"),TEXT("Activate"));
	CreatePin(EGPD_Input,TEXT("QuestPrerequisite"),TEXT("Prerequisites"));
	CreatePin(EGPD_Output,TEXT("QuestActivation"),TEXT("Any Outcome"));
	if (bHasAbandonPin) CreatePin(EGPD_Output, TEXT("QuestAbandon"), TEXT("Abandoned"));
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

	const UEdGraph* Graph = GetGraph();
	if (!Graph) return;

	const FString BaseName = GetDefaultNodeBaseName();

	TSet<FString> ExistingLabels;
	for (const UEdGraphNode* Node : Graph->Nodes)
	{
		if (const UQuestlineNode_ContentBase* Other = Cast<UQuestlineNode_ContentBase>(Node))
		{
			if (Other != this)
			{
				ExistingLabels.Add(Other->NodeLabel.ToString());
			}
		}
	}

	FString Candidate = BaseName;
	int32 Counter = 1;
	while (ExistingLabels.Contains(Candidate))
	{
		Candidate = FString::Printf(TEXT("%s_%d"), *BaseName, Counter++);
	}
	NodeLabel = FText::FromString(Candidate);
}

void UQuestlineNode_ContentBase::PostDuplicate(bool bDuplicateForPIE)
{
	Super::PostDuplicate(bDuplicateForPIE);
	QuestGuid = FGuid::NewGuid();
}

void UQuestlineNode_ContentBase::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UQuestlineNode_ContentBase, bHasAbandonPin))
	{
		if (bHasAbandonPin)
		{
			if (!FindPin(TEXT("Abandoned")))
			{
				CreatePin(EGPD_Output, TEXT("QuestAbandon"), TEXT("Abandoned"));
			}
		}
		else if (UEdGraphPin* AbandonPin = FindPin(TEXT("Abandoned")))
		{
			AbandonPin->BreakAllPinLinks();
			RemovePin(FindPin(TEXT("Abandoned")));
		}
		
		if (UEdGraph* Graph = GetGraph())
		{
			Graph->NotifyGraphChanged();
		}
	}
}

void UQuestlineNode_ContentBase::OnRenameNode(const FString& NewName)
{
	Modify();
	NodeLabel = FText::FromString(NewName);

	if (UEdGraph* Graph = GetGraph())
	{
		Graph->NotifyGraphChanged();
	}	
}
