// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Nodes/Groups/QuestlineNode_PortalExitBase.h"
#include "QuestlineNode_PrerequisiteOutcome.generated.h"

/**
 * Prerequisite leaf that gates on an outcome tag, context-free. Satisfied at runtime when ANY quest has resolved
 * with the picked OutcomeTag (or any descendant via gameplay-tag hierarchy). Decoupled from upstream graph
 * topology — unlike pin-wired prereqs which key by a specific source quest × output pin, this node authors
 * directly against an outcome the project recognizes.
 *
 * Use this for "did the player ever achieve <outcome>" gating that doesn't care WHICH quest produced it.
 * For outcome-scoped-to-a-specific-quest semantics, compose via Prereq Rules instead (a Rule combines an
 * outcome leaf with a quest-tag selector through the combinator subgraph).
 *
 * Renders via SGraphNode_GroupNode's getter branch — title bar above, inline tag picker on the same row as
 * the output pin. Tag picker is filtered to SimpleQuest.Outcome so only outcome tags are selectable.
 */
UCLASS()
class SIMPLEQUESTEDITOR_API UQuestlineNode_PrerequisiteOutcome : public UQuestlineNode_PortalExitBase
{
	GENERATED_BODY()

public:
	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual void GetPinHoverText(const UEdGraphPin& Pin, FString& HoverTextOut) const override;

	virtual FGameplayTag GetGroupTag() const override { return OutcomeTag; }
	virtual void SetGroupTag(const FGameplayTag& NewTag) override { OutcomeTag = NewTag; }
	virtual FString GetTagFilterString() const override { return TEXT("SimpleQuest.Outcome"); }

	/** The outcome tag this prereq leaf monitors. Satisfied at runtime when any quest has resolved with this
	 *  outcome (or any descendant via gameplay-tag hierarchy). Property name preserved for clarity in Details
	 *  panel; the base-class GroupTag virtuals route through it for SGraphNode_GroupNode rendering. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Prerequisite", meta = (Categories = "SimpleQuest.Outcome"))
	FGameplayTag OutcomeTag;
};