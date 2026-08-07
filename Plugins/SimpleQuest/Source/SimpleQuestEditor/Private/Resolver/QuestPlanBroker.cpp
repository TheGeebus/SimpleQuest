// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "Resolver/QuestPlanBroker.h"

FQuestPlanBroker& FQuestPlanBroker::Get()
{
	static FQuestPlanBroker Instance;
	return Instance;
}

void FQuestPlanBroker::Publish(const FString& TargetAssetPath, const FQuestInPlacePlan& Plan)
{
	PlanByAsset.Add(TargetAssetPath, Plan);
	PlanPublished.Broadcast(TargetAssetPath, Plan);
}

const FQuestInPlacePlan* FQuestPlanBroker::Find(const FString& TargetAssetPath) const
{
	return PlanByAsset.Find(TargetAssetPath);
}

void FQuestPlanBroker::Clear(const FString& TargetAssetPath)
{
	PlanByAsset.Remove(TargetAssetPath);
}

