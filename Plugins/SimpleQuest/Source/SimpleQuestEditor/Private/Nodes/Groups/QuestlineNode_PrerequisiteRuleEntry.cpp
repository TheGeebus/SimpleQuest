// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Nodes/Groups/QuestlineNode_PrerequisiteRuleEntry.h"

#include "Nodes/Prerequisites/QuestlineNode_PrerequisiteAnd.h"
#include "SimpleQuestLog.h"
#include "Types/QuestPinRole.h"
#include "Utilities/SimpleQuestEditorUtils.h"

void UQuestlineNode_PrerequisiteRuleEntry::AllocateDefaultPins()
{
    CreatePin(EGPD_Input,  TEXT("QuestPrerequisite"), TEXT("Enter"));
    CreatePin(EGPD_Output, TEXT("QuestPrerequisite"), TEXT("Forward"));
}

FText UQuestlineNode_PrerequisiteRuleEntry::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
    return NSLOCTEXT("SimpleQuestEditor", "PrereqRuleEntryTitle", "Prerequisite Rule: Entry");
}

FLinearColor UQuestlineNode_PrerequisiteRuleEntry::GetNodeTitleColor() const
{
    return SQ_ED_NODE_PREREQ_GROUP;
}

void UQuestlineNode_PrerequisiteRuleEntry::GetNodeContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const
{
    Super::GetNodeContextMenuActions(Menu, Context);

    FToolMenuSection& Section = Menu->AddSection(
        TEXT("PrerequisiteRule"),
        NSLOCTEXT("SimpleQuestEditor", "PrerequisiteRuleSection", "Prerequisite Rule")
    );

    FSimpleQuestEditorUtilities::AddExamineGroupConnectionsEntry(
        Section,
        const_cast<UQuestlineNode_PrerequisiteRuleEntry*>(this),
        GetGroupTag()
    );
}

void UQuestlineNode_PrerequisiteRuleEntry::GetPinHoverText(const UEdGraphPin& Pin, FString& HoverTextOut) const
{
    switch (GetPinRole(&Pin))
    {
    case EQuestPinRole::PrereqIn:
        HoverTextOut = TEXT(
            "The expression entering here defines this rule. When it evaluates true,\n"
            "this rule's tag publishes to WorldState, notifying every Prerequisite\n"
            "Rule Exit with a matching tag anywhere in the project.\n"
            "\n"
            "The expression's current boolean value is also available locally through\n"
            "the Forward pin.");
        break;

    case EQuestPinRole::PrereqOut:
        HoverTextOut = TEXT(
            "Forwards this rule's evaluated boolean for use in this graph —\n"
            "equivalent to placing a Prerequisite Rule Exit next to this Entry\n"
            "and wiring from it.\n"
            "\n"
            "Use a Prerequisite Rule Exit to reference this rule from any graph\n"
            "in the project.");
        break;

    default:
        Super::GetPinHoverText(Pin, HoverTextOut);
        break;
    }
}

void UQuestlineNode_PrerequisiteRuleEntry::PostLoad()
{
    Super::PostLoad();

    // Wave 4.b migration: legacy layout had multiple Condition_N inputs + a PrereqOut output.
    // New layout has a single Enter input + a Forward output. Collect legacy sources, then:
    //   - Zero wired conditions: log and leave Enter unwired.
    //   - One wired condition: wire source directly into Enter.
    //   - Multi wired conditions: spawn an AND combinator, wire sources into it, AND output into Enter.
    // Also rename legacy PrereqOut output pin to Forward (same role, new name).

    UEdGraph* OwnerGraph = GetGraph();
    if (!OwnerGraph) return;

    // Gather legacy Condition_N input pins with their LinkedTo sources.
    TArray<UEdGraphPin*> LegacyConditionPins;
    TArray<UEdGraphPin*> LegacySources;
    for (UEdGraphPin* Pin : Pins)
    {
        if (!Pin || Pin->Direction != EGPD_Input) continue;
        if (Pin->PinType.PinCategory != TEXT("QuestPrerequisite")) continue;
        if (!Pin->PinName.ToString().StartsWith(TEXT("Condition_"))) continue;
        LegacyConditionPins.Add(Pin);
        for (UEdGraphPin* Linked : Pin->LinkedTo)
        {
            if (Linked) LegacySources.Add(Linked);
        }
    }

    const bool bHasLegacyLayout = LegacyConditionPins.Num() > 0;

    // Ensure the new Enter pin exists (AllocateDefaultPins may not have been called for a legacy-serialized node).
    UEdGraphPin* EnterPin = GetPinByRole(EQuestPinRole::PrereqIn);
    if (!EnterPin && bHasLegacyLayout)
    {
        EnterPin = CreatePin(EGPD_Input, TEXT("QuestPrerequisite"), TEXT("Enter"));
    }

    // Migrate sources into Enter.
    if (bHasLegacyLayout && EnterPin)
    {
        if (LegacySources.Num() == 0)
        {
            UE_LOG(LogSimpleQuest, Warning,
                TEXT("UQuestlineNode_PrerequisiteRuleEntry::PostLoad: '%s' (GUID %s) had legacy layout with zero wired conditions; Enter remains unwired."),
                *GetName(), *NodeGuid.ToString());
        }
        else if (LegacySources.Num() == 1)
        {
            EnterPin->MakeLinkTo(LegacySources[0]);
            UE_LOG(LogSimpleQuest, Log,
                TEXT("UQuestlineNode_PrerequisiteRuleEntry::PostLoad: '%s' (GUID %s) migrated — single legacy source wired directly into Enter."),
                *GetName(), *NodeGuid.ToString());
        }
        else
        {
            // Spawn an AND combinator adjacent to this Entry.
            FGraphNodeCreator<UQuestlineNode_PrerequisiteAnd> Creator(*OwnerGraph);
            UQuestlineNode_PrerequisiteAnd* AndNode = Creator.CreateNode(false);
            AndNode->NodePosX = NodePosX - 200;
            AndNode->NodePosY = NodePosY;
            Creator.Finalize();

            // Ensure AND has enough condition pins to accept all legacy sources (AllocateDefaultPins typically creates 2).
            while (AndNode->GetConditionPinCount() < LegacySources.Num())
            {
                AndNode->AddConditionPin();
            }

            // Wire each legacy source into a unique AND condition pin (by index).
            int32 CondIndex = 0;
            for (UEdGraphPin* Source : LegacySources)
            {
                UEdGraphPin* AndCondPin = AndNode->FindPin(*FString::Printf(TEXT("Condition_%d"), CondIndex++), EGPD_Input);
                if (AndCondPin && Source) AndCondPin->MakeLinkTo(Source);
            }

            // Wire AND's output into Enter.
            UEdGraphPin* AndOut = AndNode->GetPinByRole(EQuestPinRole::PrereqOut);
            if (AndOut) AndOut->MakeLinkTo(EnterPin);

            UE_LOG(LogSimpleQuest, Log,
                TEXT("UQuestlineNode_PrerequisiteRuleEntry::PostLoad: '%s' (GUID %s) migrated — %d legacy sources wired through new AND combinator (GUID %s) into Enter."),
                *GetName(), *NodeGuid.ToString(), LegacySources.Num(), *AndNode->NodeGuid.ToString());
        }

        // Break remaining links on legacy Condition_N pins and remove them.
        for (UEdGraphPin* LegacyPin : LegacyConditionPins)
        {
            LegacyPin->BreakAllPinLinks();
            RemovePin(LegacyPin);
        }
    }

    // Rename legacy PrereqOut output pin to Forward (preserves LinkedTo via in-place mutation).
    int32 RenamedOutputs = 0;
    for (UEdGraphPin* Pin : Pins)
    {
        if (Pin && Pin->Direction == EGPD_Output
            && Pin->PinType.PinCategory == TEXT("QuestPrerequisite")
            && Pin->PinName == TEXT("PrereqOut"))
        {
            Pin->PinName = TEXT("Forward");
            ++RenamedOutputs;
        }
    }
    if (RenamedOutputs > 0)
    {
        UE_LOG(LogSimpleQuest, Log,
            TEXT("UQuestlineNode_PrerequisiteRuleEntry::PostLoad: '%s' — %d 'PrereqOut' output pin(s) renamed to 'Forward'."),
            *GetName(), RenamedOutputs);
    }
}

