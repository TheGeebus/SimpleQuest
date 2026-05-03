// Copyright 2026, Greg Bussell, All Rights Reserved.

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

	/** Widget accessors — concrete classes implement these. */
	virtual const FGameplayTagContainer& GetTargetQuestTags() const
	{
		static const FGameplayTagContainer Empty;
		return Empty;
	}
	virtual void SetTargetQuestTags(const FGameplayTagContainer& NewTags) {}
	virtual FString GetTargetQuestTagsFilterString() const { return TEXT("Quest"); }
};
