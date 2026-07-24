// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "QuestlineNode_PrerequisiteBase.h"
#include "QuestlineNode_PrerequisiteOr.generated.h"

UCLASS()
class SIMPLEQUESTEDITOR_API UQuestlineNode_PrerequisiteOr : public UQuestlineNode_PrerequisiteBase
{
	GENERATED_BODY()
public:
	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual void GetNodeContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const override;

	void AddConditionPin();
	/**
	 * Re-sync the Condition_N input pins to the current ConditionPinCount. Used by the import to rebuild pins after
	 * ConditionPinCount is restored onto a node allocated with the default count.
	 */
	void SyncConditionPins();
	FORCEINLINE int32 GetConditionPinCount() const { return ConditionPinCount; } 

private:
	/**
	 * Authored via the context-menu Add/Remove Condition Pin actions, not the Details panel — hence no EditAnywhere.
	 * meta=(QuestExport) marks it authored config for the interlingua export despite the missing CPF_Edit.
	 */
	UPROPERTY(meta = (QuestExport))
	int32 ConditionPinCount = 2;

	void RemoveConditionPin(UEdGraphPin* PinToRemove);
};
