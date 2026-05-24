// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Nodes/Utility/QuestlineNode_UtilityBase.h"
#include "QuestlineNode_PrereqGate.generated.h"

/**
 * Editor graph node that gates the activation cascade on a Prerequisites input expression. Forward output fires when
 * the expression evaluates true — immediately if satisfied at activation, deferred until satisfied otherwise.
 * Compiles to UPrereqGateNode; reuses UQuestNodeBase's prereq evaluation + deferred subscription machinery.
 */
UCLASS()
class SIMPLEQUESTEDITOR_API UQuestlineNode_PrereqGate : public UQuestlineNode_UtilityBase
{
	GENERATED_BODY()

public:
	virtual void AllocateDefaultPins() override;

	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override
	{
		return NSLOCTEXT("SimpleQuestEditor", "PrereqGateTitle", "Prereq Gate");
	}

	/**
	 * Override the utility-base label suppression. PrereqGate is the first utility node with two same-direction
	 * input pins (Enter + Prerequisites), so the single-input geometry assumption that motivated label suppression
	 * on the base doesn't apply. Wire colors alone are insufficient disambiguation for adopters not yet fluent in
	 * the schema color convention.
	 */
	virtual FText GetPinDisplayName(const UEdGraphPin* Pin) const override;

	/** Per-pin hover tooltips explaining each pin's role in the gate's evaluation flow. */
	virtual void GetPinHoverText(const UEdGraphPin& Pin, FString& HoverTextOut) const override;

	/** Node-level tooltip describing the gate's per-activation semantics at a glance. */
	virtual FText GetTooltipText() const override;
};