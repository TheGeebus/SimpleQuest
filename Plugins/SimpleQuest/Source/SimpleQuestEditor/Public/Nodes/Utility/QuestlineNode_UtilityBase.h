// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Nodes/QuestlineNodeBase.h"
#include "QuestlineNode_UtilityBase.generated.h"

UCLASS(Abstract)
class SIMPLEQUESTEDITOR_API UQuestlineNode_UtilityBase : public UQuestlineNodeBase
{
	GENERATED_BODY()

public:
	virtual void PostLoad() override;
	virtual void AllocateDefaultPins() override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual bool IsUtilityNode() const override { return true; }
	virtual bool CanUserDeleteNode() const override { return true; }
	virtual bool CanDuplicateNode() const override { return true; }
	virtual FLinearColor GetNodeTitleColor() const override;
	
	/** Suppress pin labels — widget handles visual layout without labels. */
	virtual FText GetPinDisplayName(const UEdGraphPin* Pin) const override { return FText::GetEmpty(); }

	/**
	 * Widget accessors — concrete classes implement these. Default returns empty / generic values so subclasses
	 * without a tag-picker authoring row (StartQuestline, PrereqGate) need no overrides.
	 */
	virtual const FGameplayTagContainer& GetTargetQuestTags() const
	{
		static const FGameplayTagContainer Empty;
		return Empty;
	}
	virtual void SetTargetQuestTags(const FGameplayTagContainer& NewTags) {}
	virtual FString GetTargetQuestTagsFilterString() const { return TEXT("Quest"); }

	/**
	 * Designer-facing label for the tag-picker authoring row. Overridden per concrete utility node so each picker
	 * names what it edits (SetBlocked → "Tags to Block:", AddFact → "Facts to Add:", etc.). Default is a generic
	 * "Tags:" for any utility node that surfaces a picker without overriding.
	 */
	virtual FText GetAuthoringTagsLabel() const
	{
		return NSLOCTEXT("SimpleQuestEditor", "DefaultAuthoringTagsLabel", "Tags:");
	}
};
