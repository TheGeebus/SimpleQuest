// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Quests/QuestNodeBase.h"
#include "QuestPrereqRuleNode.generated.h"

struct FWorldStateFactAddedEvent;
struct FQuestResolutionRecordedEvent;

UCLASS()
class SIMPLEQUEST_API UQuestPrereqRuleNode : public UQuestNodeBase
{
	GENERATED_BODY()
	friend class FQuestlineGraphCompiler;

public:
	virtual void Activate(FGameplayTag InContextualTag) override;

protected:
	virtual void ResetTransientState() override;

private:
	/** The fact written to WorldState when Expression evaluates true. */
	UPROPERTY() FGameplayTag GroupTag;

	/** The compiled prereq expression tree. Publishes GroupTag when it evaluates true against WorldState. */
	UPROPERTY() FPrerequisiteExpression Expression;

	/** Per-leaf subscription handles for re-evaluation on leaf events. Keyed by FactTag for fact leaves and by
		ResolutionQuestTag for resolution leaves — both are FGameplayTag, so the map key shape is uniform. */
	TMap<FGameplayTag, FDelegateHandle> SubscriptionHandles;

	void OnLeafFactAdded(FGameplayTag Channel, const FWorldStateFactAddedEvent& Event);
	void OnLeafResolutionRecorded(FGameplayTag Channel, const FQuestResolutionRecordedEvent& Event);
	void TryPublishRule();
};
