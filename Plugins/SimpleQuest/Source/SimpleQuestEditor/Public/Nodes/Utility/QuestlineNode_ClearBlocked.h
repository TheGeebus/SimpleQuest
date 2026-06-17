#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Nodes/Utility/QuestlineNode_UtilityBase.h"
#include "QuestlineNode_ClearBlocked.generated.h"

UCLASS()
class SIMPLEQUESTEDITOR_API UQuestlineNode_ClearBlocked : public UQuestlineNode_UtilityBase
{
	GENERATED_BODY()

public:
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override
	{
		return NSLOCTEXT("SimpleQuestEditor", "ClearBlockedTitle", "Clear Blocked");
	}

	virtual FText GetAuthoringTagsLabel() const override
	{
		return NSLOCTEXT("SimpleQuestEditor", "ClearBlockedLabel", "Tags to Clear:");
	}

	virtual const FGameplayTagContainer& GetTargetQuestTags() const override { return TargetQuestTags; }
	virtual void SetTargetQuestTags(const FGameplayTagContainer& NewTags) override { TargetQuestTags = NewTags; }
	virtual FString GetTargetQuestTagsFilterString() const override { return TEXT("SimpleQuest.Questline"); }

	UPROPERTY(EditAnywhere, Category="Blocked", meta=(Categories="SimpleQuest.Questline"))
	FGameplayTagContainer TargetQuestTags;
};