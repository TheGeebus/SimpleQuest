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

    FToolMenuSection& Section = Menu->AddSection(TEXT("PrerequisiteRule"), NSLOCTEXT("SimpleQuestEditor", "PrerequisiteRuleSection", "Prerequisite Rule"));

    FSimpleQuestEditorUtilities::AddExaminePrereqExpressionEntry(Section, const_cast<UQuestlineNode_PrerequisiteRuleEntry*>(this));
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

    // Minimal migration: rename legacy PrereqOut output → Forward in place (preserves LinkedTo).
    // No pin creation, no pin removal, no structural changes. Legacy multi-condition authoring
    // must be re-wired manually by the designer — the automatic AND-combinator migration proved
    // unsafe and introduced double-pin / missing-pin regressions.
    for (UEdGraphPin* Pin : Pins)
    {
        if (Pin && Pin->Direction == EGPD_Output
            && Pin->PinType.PinCategory == TEXT("QuestPrerequisite")
            && Pin->PinName == TEXT("PrereqOut"))
        {
            Pin->PinName = TEXT("Forward");
        }
    }
}

