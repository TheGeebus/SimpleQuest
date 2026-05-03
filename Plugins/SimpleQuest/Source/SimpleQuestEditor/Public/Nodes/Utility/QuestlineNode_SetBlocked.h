#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Nodes/Utility/QuestlineNode_UtilityBase.h"
#include "QuestlineNode_SetBlocked.generated.h"

UCLASS()
class SIMPLEQUESTEDITOR_API UQuestlineNode_SetBlocked : public UQuestlineNode_UtilityBase
{
	GENERATED_BODY()

public:
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override
	{
		return NSLOCTEXT("SimpleQuestEditor", "SetBlockedTitle", "Set Blocked");
	}

	virtual const FGameplayTagContainer& GetTargetQuestTags() const override { return TargetQuestTags; }
	virtual void SetTargetQuestTags(const FGameplayTagContainer& NewTags) override { TargetQuestTags = NewTags; }
	virtual FString GetTargetQuestTagsFilterString() const override { return TEXT("SimpleQuest.Quest"); }

	UPROPERTY(EditAnywhere, Category="Blocked", meta=(Categories="SimpleQuest.Quest"))
	FGameplayTagContainer TargetQuestTags;

	/**
	 * When true, also issues a deactivation request for each target quest in addition to setting the Blocked
	 * re-entry gate. When false (default), Set Blocked only sets the gate — any in-flight lifecycle on the
	 * targets continues to its current resolution. Block is purely a future-activation gate by default;
	 * this toggle opts into interrupting in-flight quests as well.
	 */
	UPROPERTY(EditAnywhere, Category="Blocked", meta=(DisplayName="Also Deactivate Targets"))
	bool bAlsoDeactivateTargets = false;
};