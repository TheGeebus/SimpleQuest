#include "Nodes/QuestlineNode_ContentBase.h"

#include "Framework/Notifications/NotificationManager.h"
#include "Nodes/QuestlineNode_Quest.h"
#include "Quests/QuestlineGraph.h"
#include "Quests/QuestNodeBase.h"
#include "Utilities/SimpleQuestEditorUtils.h"
#include "Widgets/Notifications/SNotificationList.h"

UQuestlineNode_ContentBase::UQuestlineNode_ContentBase()
{
	bCanRenameNode = true;
}

void UQuestlineNode_ContentBase::AllocateDefaultPins()
{
	// Input
	CreatePin(EGPD_Input, TEXT("QuestActivation"), TEXT("Activate"));
	CreatePin(EGPD_Input, TEXT("QuestPrerequisite"), TEXT("Prerequisites"));

	// Ouput
	CreatePin(EGPD_Output, TEXT("QuestActivation"), TEXT("Any Outcome"));
	
	AllocateOutcomePins(); // virtual hook: subclasses can create additional output pins between Activate and Deactivate.

	if (bShowDeactivationPins)
	{
		CreatePin(EGPD_Input, TEXT("QuestDeactivate"), TEXT("Deactivate"));
		CreatePin(EGPD_Output, TEXT("QuestDeactivated"), TEXT("Deactivated"));
	}
}

void UQuestlineNode_ContentBase::PostPlacedNewNode()
{
	Super::PostPlacedNewNode();

	// Seed with subclass's default base name; helper sweeps for uniqueness (suffixing "_N" if needed).
	NodeLabel = FText::FromString(GetDefaultNodeBaseName());
	EnsureUniqueLabel();
}

void UQuestlineNode_ContentBase::PostDuplicate(bool bDuplicateForPIE)
{
	Super::PostDuplicate(bDuplicateForPIE);
	// StaticDuplicateObject path — defense-in-depth. Not the user Ctrl-D / paste path (that's PostPasteNode)
	// but anything that duplicates programmatically lands here.
	EnsureUniqueLabel();
}

void UQuestlineNode_ContentBase::PostPasteNode()
{
	Super::PostPasteNode();
	// THE user-facing duplicate path. SGraphEditor's Ctrl-D and copy-paste both go through ExportText/ImportText
	// which instantiates via NewObject and calls PostPasteNode (not PostDuplicate). Without this override, pasted
	// nodes retained the source's QuestGuid and NodeLabel, which the compiler rejects as duplicate identity.
	EnsureUniqueLabel();
}

void UQuestlineNode_ContentBase::EnsureUniqueLabel()
{
	// QuestGuid regeneration moved to UQuestlineNodeBase::PostPlacedNewNode / PostDuplicate / PostPasteNode.
	// Every editor node type now uniformly gets a fresh GUID on placement / duplication / paste. This method
	// retains the label-uniqueness sweep below — content-specific behavior that doesn't apply to utility / portal
	// nodes.
	
	const UEdGraph* Graph = GetGraph();
	if (!Graph) return;

	// Parse current label into (RootName, OriginalCounter). If no trailing "_N", counter is 0 and RootName is the
	// full label. Re-duplicating "GetBook_1" produces "GetBook_2" rather than "GetBook_1_1".
	const FString CurrentLabel = NodeLabel.ToString();
	FString RootName = CurrentLabel;
	int32 OriginalCounter = 0;

	int32 UnderscoreIdx;
	if (CurrentLabel.FindLastChar(TEXT('_'), UnderscoreIdx))
	{
		const FString Trailing = CurrentLabel.Mid(UnderscoreIdx + 1);
		if (!Trailing.IsEmpty() && Trailing.IsNumeric())
		{
			RootName = CurrentLabel.Left(UnderscoreIdx);
			OriginalCounter = FCString::Atoi(*Trailing);
		}
	}
	if (RootName.IsEmpty())
	{
		RootName = GetDefaultNodeBaseName();
	}

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

	// Sweep for the first unused candidate. Start at the original label (counter 0 = bare RootName, >0 = "RootName_N"),
	// then increment until unused. Counter 0 case handles fresh-placed nodes where the root is unused in this graph.
	auto MakeLabel = [&](int32 N)
	{
		return (N == 0) ? RootName : FString::Printf(TEXT("%s_%d"), *RootName, N);
	};
	int32 Counter = OriginalCounter;
	FString Candidate = MakeLabel(Counter);
	while (ExistingLabels.Contains(Candidate))
	{
		++Counter;
		Candidate = MakeLabel(Counter);
	}
	NodeLabel = FText::FromString(Candidate);
}

void UQuestlineNode_ContentBase::PreEditChange(FProperty* PropertyAboutToChange)
{
	Super::PreEditChange(PropertyAboutToChange);
	if (PropertyAboutToChange && PropertyAboutToChange->GetFName() == GET_MEMBER_NAME_CHECKED(UQuestlineNode_ContentBase, NodeLabel))
	{
		PreEditNodeLabelSnapshot = NodeLabel;
	}
}

void UQuestlineNode_ContentBase::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropName = PropertyChangedEvent.GetPropertyName();

	if (PropName == GET_MEMBER_NAME_CHECKED(UQuestlineNode_ContentBase, NodeLabel))
	{
		// Validate the new label against live + compiled state on the same graph. Reject + revert if it collides
		// with another node — the alternative is letting the compile-time redirect machinery silently drop one of
		// the two renames, which can lose subscriber linkage on the orphaned node.
		FText ErrorText;
		if (!IsLabelAvailable(NodeLabel.ToString(), ErrorText))
		{
			NodeLabel = PreEditNodeLabelSnapshot;

			FNotificationInfo Info(ErrorText);
			Info.ExpireDuration = 6.f;
			Info.bUseSuccessFailIcons = true;
			FSlateNotificationManager::Get().AddNotification(Info)->SetCompletionState(SNotificationItem::CS_Fail);
			return;
		}

		// Details-panel rename goes through UPROPERTY change, not OnRenameNode. Route through the shared notify path
		// so the stale-tag warning appears on this widget AND on descendant widgets whose paths include this label.
		NotifyRenameSideEffects();
		return;
	}

	if (PropName == GET_MEMBER_NAME_CHECKED(UQuestlineNode_ContentBase, bShowDeactivationPins))
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
	NotifyRenameSideEffects();
}

void UQuestlineNode_ContentBase::NotifyRenameSideEffects()
{
	if (UEdGraph* Graph = GetGraph()) Graph->NotifyGraphChanged();
	// Container nodes (Quest) override NotifyInnerGraphsOfRename to walk descendants — a Step inside Inner sees
	// its reconstructed tag path change whenever Inner's label changes, so its widget needs to re-evaluate too.
	NotifyInnerGraphsOfRename();
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

bool UQuestlineNode_ContentBase::IsLabelAvailable(const FString& ProposedLabel, FText& OutError) const
{
	UEdGraph* MyGraph = GetGraph();
	if (!MyGraph) return true;  // can't validate without a graph context — let it through

	// Walk Outer chain to the owning UQuestlineGraph asset (skipping any intermediate Quest container graphs).
	UObject* Outer = MyGraph->GetOuter();
	while (UQuestlineNode_Quest* QuestNode = Cast<UQuestlineNode_Quest>(Outer))
	{
		UEdGraph* QuestGraph = QuestNode->GetGraph();
		if (!QuestGraph) break;
		Outer = QuestGraph->GetOuter();
	}
	const UQuestlineGraph* OwningAsset = Cast<UQuestlineGraph>(Outer);

	for (UEdGraphNode* OtherEdNode : MyGraph->Nodes)
	{
		const UQuestlineNode_ContentBase* OtherContent = Cast<UQuestlineNode_ContentBase>(OtherEdNode);
		if (!OtherContent || OtherContent == this) continue;

		// Live label collision — another node on this graph currently displays the proposed label.
		if (OtherContent->NodeLabel.ToString().Equals(ProposedLabel))
		{
			OutError = FText::Format(NSLOCTEXT("SimpleQuestEditor", "RenameCollision_LiveLabel",
				"Cannot rename to '{0}' — another node on this graph is currently named '{0}'."),
				FText::FromString(ProposedLabel));
			return false;
		}

		// Compiled-identity collision — another node on this graph still holds the proposed label as its last-compiled
		// identity. The other node has been renamed in the editor but not yet recompiled, so its previous canonical
		// name is still pinned in the compiled tag registry. Allowing this rename would create the in-batch collision
		// at the next compile.
		if (OwningAsset)
		{
			for (const TPair<FName, TObjectPtr<UQuestNodeBase>>& Compiled : OwningAsset->GetCompiledNodes())
			{
				const UQuestNodeBase* CompiledInstance = Compiled.Value;
				if (!CompiledInstance) continue;
				if (CompiledInstance->GetQuestGuid() != OtherContent->QuestGuid) continue;

				// Found the compiled record for OtherContent. Extract the leaf segment of its compiled tag and compare.
				const FString TagStr = Compiled.Key.ToString();
				int32 LastDot = INDEX_NONE;
				const bool bHasDot = TagStr.FindLastChar(TEXT('.'), LastDot);
				const FString CompiledLeaf = bHasDot ? TagStr.Mid(LastDot + 1) : TagStr;

				if (CompiledLeaf.Equals(ProposedLabel))
				{
					OutError = FText::Format(NSLOCTEXT("SimpleQuestEditor", "RenameCollision_CompiledIdentity",
						"Cannot rename to '{0}' — another node on this graph still holds '{0}' as its compiled identity. Recompile the questline to lock in pending renames and free the name."),
						FText::FromString(ProposedLabel));
					return false;
				}
				break;  // found the matching compiled record; no need to keep scanning for this OtherContent
			}
		}
	}

	return true;
}

