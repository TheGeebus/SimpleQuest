// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Resolver/QuestPlanBroker.h"

FQuestPlanBroker& FQuestPlanBroker::Get()
{
	static FQuestPlanBroker Instance;
	return Instance;
}

void FQuestPlanBroker::Publish(const FString& TargetAssetPath, const FQuestInPlacePlan& Plan, const FQuestPlanSource& Source)
{
	FQuestPlanRecord& Record = RecordByAsset.FindOrAdd(TargetAssetPath);
	Record.Plan = Plan;
	Record.Source = Source;
	Record.Error.Empty();
	PlanPublished.Broadcast(TargetAssetPath, Record.Plan);
}

void FQuestPlanBroker::PublishFailure(const FString& TargetAssetPath, const FString& Reason, const FQuestPlanSource& Source)
{
	FQuestPlanRecord& Record = RecordByAsset.FindOrAdd(TargetAssetPath);
	Record.Source = Source;
	Record.Error  = Reason;
	// The previous plan is deliberately left in place but is no longer the current answer - consumers gate on Error
	// first. Discarding it would lose a plan the designer may still be reading while they fix the source.
	PlanPublished.Broadcast(TargetAssetPath, Record.Plan);
}

const FQuestPlanRecord* FQuestPlanBroker::Find(const FString& TargetAssetPath) const
{
	return RecordByAsset.Find(TargetAssetPath);
}

void FQuestPlanBroker::Clear(const FString& TargetAssetPath)
{
	RecordByAsset.Remove(TargetAssetPath);
}