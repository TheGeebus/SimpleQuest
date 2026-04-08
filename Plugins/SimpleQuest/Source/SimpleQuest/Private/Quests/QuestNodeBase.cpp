// Copyright 2026, Greg Bussell, All Rights Reserved.

#include "Quests/QuestNodeBase.h"
#include "GameplayTagsManager.h"
#include "Quests/PrerequisiteExpression.h"


void UQuestNodeBase::Activate(FGameplayTag InContextualTag)
{
	SetContextualTag(InContextualTag);
	OnNodeActivated.ExecuteIfBound(this, InContextualTag);
}

void UQuestNodeBase::ResolveQuestTag(FName TagName)
{
	QuestTag = UGameplayTagsManager::Get().RequestGameplayTag(TagName);
}

const TArray<FName>* UQuestNodeBase::GetNextNodesForOutcome(FGameplayTag OutcomeTag) const
{
	const FQuestOutcomeNodeList* List = NextNodesByOutcome.Find(OutcomeTag);
	return List ? &List->NodeTags : nullptr;
}

