#include "Nodes/QuestlineNode_ContentBase.h"

#include "Utilities/SimpleQuestEditorUtils.h"

UQuestlineNode_ContentBase::UQuestlineNode_ContentBase()
{
	bCanRenameNode = true;
}

void UQuestlineNode_ContentBase::AllocateDefaultPins()
{
	// Input
	CreatePin(EGPD_Input,  TEXT("QuestActivation"),   TEXT("Activate"));
	CreatePin(EGPD_Input,  TEXT("QuestPrerequisite"),  TEXT("Prerequisites"));

	// Ouput
	CreatePin(EGPD_Output, TEXT("QuestActivation"),    TEXT("Any Outcome"));
	AllocateOutcomePins(); // virtual hook: subclasses can create additional output pins between Activate and Deactivate.
	if (bShowDeactivationPins)
	{
		CreatePin(EGPD_Input,  TEXT("QuestDeactivate"),  TEXT("Deactivate"));
		CreatePin(EGPD_Output, TEXT("QuestDeactivated"), TEXT("Deactivated"));
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
	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UQuestlineNode_ContentBase, bShowDeactivationPins))
	{
		Modify();

		if (bShowDeactivationPins)
		{
			// Un-orphan if they already exist as stale pins; create only if truly absent
			if (UEdGraphPin* Pin = FindPin(TEXT("Deactivate")))
			{
				Pin->bOrphanedPin = false;
			}
			else
			{
				CreatePin(EGPD_Input, TEXT("QuestDeactivate"), TEXT("Deactivate"));
			}

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
			if (UEdGraphPin* Pin = FindPin(TEXT("Deactivate")))
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

void UQuestlineNode_ContentBase::OnRenameNode(const FString& NewName)
{
	Modify();
	NodeLabel = FText::FromString(NewName);

	if (UEdGraph* Graph = GetGraph())
	{
		Graph->NotifyGraphChanged();
	}	
}

void UQuestlineNode_ContentBase::EnsureDeactivationPinsForAutowire()
{
	if (bShowDeactivationPins) return;

	Modify();
	bShowDeactivationPins = true;

	if (UEdGraphPin* Pin = FindPin(TEXT("Deactivate")))
	{
		Pin->bOrphanedPin = false;
	}
	else
	{
		CreatePin(EGPD_Input, TEXT("QuestDeactivate"), TEXT("Deactivate"));
	}

	if (UEdGraphPin* Pin = FindPin(TEXT("Deactivated")))
	{
		Pin->bOrphanedPin = false;
	}
	else
	{
		CreatePin(EGPD_Output, TEXT("QuestDeactivated"), TEXT("Deactivated"));
	}

	if (UEdGraph* Graph = GetGraph())
	{
		Graph->NotifyGraphChanged();
	}
}

void UQuestlineNode_ContentBase::GetNodeContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const
{
	Super::GetNodeContextMenuActions(Menu, Context);

	FToolMenuSection& Section = Menu->AddSection(TEXT("ContentExaminer"), NSLOCTEXT("SimpleQuestEditor", "ContentExaminerSection", "Prerequisite"));

	FSimpleQuestEditorUtilities::AddExaminePrereqExpressionEntry(Section, const_cast<UQuestlineNode_ContentBase*>(this));
}
