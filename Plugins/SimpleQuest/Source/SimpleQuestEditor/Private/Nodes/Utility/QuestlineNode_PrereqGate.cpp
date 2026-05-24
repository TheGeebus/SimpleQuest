// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Nodes/Utility/QuestlineNode_PrereqGate.h"

#define LOCTEXT_NAMESPACE "QuestlineNode_PrereqGate"

void UQuestlineNode_PrereqGate::AllocateDefaultPins()
{
    // Super provides Enter (input) + Forward (output). Append the Prerequisites pin after so pin iteration order is
    // Enter, Forward, Prerequisites — which the slate widget renders as Enter on top-left, Prerequisites below it,
    // Forward on the right.
    Super::AllocateDefaultPins();
    CreatePin(EGPD_Input, TEXT("QuestPrerequisite"), TEXT("Prerequisites"));
}

FText UQuestlineNode_PrereqGate::GetPinDisplayName(const UEdGraphPin* Pin) const
{
    if (!Pin) return FText::GetEmpty();
    if (Pin->PinName == TEXT("Enter"))         return LOCTEXT("PinLabelEnter", "Enter");
    if (Pin->PinName == TEXT("Prerequisites")) return LOCTEXT("PinLabelPrereqs", "Prerequisites");
    if (Pin->PinName == TEXT("Forward"))       return LOCTEXT("PinLabelForward", "Forward");
    return FText::GetEmpty();
}

void UQuestlineNode_PrereqGate::GetPinHoverText(const UEdGraphPin& Pin, FString& HoverTextOut) const
{
    if (Pin.PinName == TEXT("Enter"))
    {
        HoverTextOut = LOCTEXT("PinTooltipEnter",
            "Activates the gate. Evaluates the Prerequisites expression and fires Forward immediately if satisfied; "
            "otherwise defers until the expression flips true. Each Enter signal evaluates independently — the gate "
            "has no fired-state memory.").ToString();
    }
    else if (Pin.PinName == TEXT("Prerequisites"))
    {
        HoverTextOut = LOCTEXT("PinTooltipPrereqs",
            "Conditional expression that gates the activation cascade. Wire through combinator nodes (AND / OR / NOT) "
            "to prereq leaves (Outcome, Path, Entry, Fact, etc.).").ToString();
    }
    else if (Pin.PinName == TEXT("Forward"))
    {
        HoverTextOut = LOCTEXT("PinTooltipForward",
            "Fires when the Prerequisites expression is satisfied — immediately if satisfied at activation time, "
            "deferred until satisfaction otherwise.").ToString();
    }
}

FText UQuestlineNode_PrereqGate::GetTooltipText() const
{
    return LOCTEXT("NodeTooltip",
        "Gates the activation cascade on a prerequisite expression. Each Enter signal evaluates the wired "
        "Prerequisites; Forward fires when satisfied (immediately if pre-satisfied, deferred otherwise). "
        "Per-activation semantics — multiple Enter signals each produce their own Forward fire.");
}

#undef LOCTEXT_NAMESPACE