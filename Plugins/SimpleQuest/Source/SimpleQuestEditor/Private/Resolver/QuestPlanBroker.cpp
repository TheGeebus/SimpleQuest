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