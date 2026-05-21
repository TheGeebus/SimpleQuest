// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Nodes/Groups/QuestlineNode_PortalExitBase.h"
#include "QuestlineNode_PrerequisiteFactTag.generated.h"

/**
 * Prerequisite leaf that gates on the presence of a designer-picked World State fact tag. Decoupled from
 * graph topology — unlike pin-wired prereqs which key by which specific output pin fired, this node
 * authors directly against an arbitrary gameplay tag in the project's state vocabulary. Satisfied at
 * runtime when UWorldStateSubsystem::HasFact returns true for the picked tag.
 *
 * Use this to gate a quest on anything World State tracks — quest state facts (Live / Completed /
 * Blocked / etc.), designer-authored game state, system-level flags from outside the quest framework.
 * Composable with AND / OR / NOT combinators and other prereq leaf shapes.
 *
 * Renders via SGraphNode_GroupNode's getter branch — title bar above, inline tag picker on the same
 * row as the output pin. Matches the visual shape of UQuestlineNode_PrerequisiteRuleExit so all
 * tag-picker prereq leaves stay visually consistent.
 */
UCLASS()
class SIMPLEQUESTEDITOR_API UQuestlineNode_PrerequisiteFactTag : public UQuestlineNode_PortalExitBase
{
	GENERATED_BODY()

public:
	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual void GetPinHoverText(const UEdGraphPin& Pin, FString& HoverTextOut) const override;

	virtual FGameplayTag GetGroupTag() const override { return FactTag; }
	virtual void SetGroupTag(const FGameplayTag& NewTag) override { FactTag = NewTag; }

	/** Empty filter — any registered gameplay tag is a valid fact key. World State doesn't restrict
	 *  the namespace facts may be authored against, so neither does this picker. */
	virtual FString GetTagFilterString() const override { return FString(); }

	/** The fact tag this prereq leaf monitors. Satisfied at runtime when UWorldStateSubsystem::HasFact
	 *  returns true for this tag. Property name preserved for clarity in Details panel; the base-class
	 *  GroupTag virtuals route through it for SGraphNode_GroupNode's tag-picker rendering. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Prerequisite")
	FGameplayTag FactTag;
};