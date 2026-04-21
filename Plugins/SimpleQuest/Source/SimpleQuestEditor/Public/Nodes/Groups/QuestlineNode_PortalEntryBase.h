// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Nodes/QuestlineNodeBase.h"
#include "QuestlineNode_PortalEntryBase.generated.h"

UCLASS(Abstract)
class SIMPLEQUESTEDITOR_API UQuestlineNode_PortalEntryBase : public UQuestlineNodeBase
{
	GENERATED_BODY()

public:
	virtual bool CanUserDeleteNode() const override { return true; }
	virtual bool CanDuplicateNode() const override { return true; }
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual FText GetPinDisplayName(const UEdGraphPin* Pin) const override { return FText::GetEmpty(); }

	/** Widget queries this to decide whether to show the "Add pin" button. Default: true (prereq setter has dynamic inputs). */
	virtual bool CanAddInputPin() const { return true; }
	
	/** Widget accessors — concrete classes implement these. */
	virtual FGameplayTag GetGroupTag() const { return FGameplayTag(); }
	virtual void SetGroupTag(const FGameplayTag& NewTag) {}
	virtual FString GetTagFilterString() const { return FString(); }
	virtual void AddInputPin() {}
};