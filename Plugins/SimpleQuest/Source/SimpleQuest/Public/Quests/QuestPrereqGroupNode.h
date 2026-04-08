// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Quests/QuestNodeBase.h"
#include "QuestPrereqGroupNode.generated.h"

struct FWorldStateFactAddedEvent;

UCLASS()
class SIMPLEQUEST_API UQuestPrereqGroupNode : public UQuestNodeBase
{
	GENERATED_BODY()
	friend class FQuestlineGraphCompiler;

public:
	virtual void Activate(FGameplayTag InContextualTag) override;

private:
	/** The fact written to WorldState when all conditions are satisfied. Format: Quest.Prereq.<Name>.Satisfied */
	UPROPERTY() FGameplayTag GroupTag;

	/** Flat list of WorldState fact tags that must all be present. Compiler-written. */
	UPROPERTY() TArray<FGameplayTag> ConditionTags;

	/** Per-condition subscription handles, keyed by condition tag for clean unsubscription. */
	TMap<FGameplayTag, FDelegateHandle> SubscriptionHandles;

	void OnConditionFactAdded(const FWorldStateFactAddedEvent& Event);
	void TrySatisfyGroup();
};
