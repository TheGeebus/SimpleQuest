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
	// A failure with NO REASON is not expressible on purpose: Error is what every consumer gates on. An empty one
	// silently turns a success into "no plan" and clears the rows. Guarded here.
	if (!ensureMsgf(!Reason.IsEmpty(), TEXT("PublishFailure('%s') with an empty reason - did a SUCCESS path call this "
		"instead of Publish?"), *TargetAssetPath))
	{
		return;
	}

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
	// Broadcast unconditionally rather than only when something was removed. A consumer displaying a plan the broker
	// has no record of is precisely the state worth correcting, and "there was nothing to clear" is no reason to leave
	// it on screen.
	RecordByAsset.Remove(TargetAssetPath);
	PlanCleared.Broadcast(TargetAssetPath);
}

void FQuestPlanBroker::PublishExport(const FString& TargetAssetPath, const FString& Summary, const FString& Error)
{
	// Nothing recorded - see the header for why an export receipt is transient where a plan is not.
	ExportCompleted.Broadcast(TargetAssetPath, Summary, Error);
}

