// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

// Where a computed plan waits for whoever wants to show it. What PRODUCES a plan and what DISPLAYS one are deliberately
// unaware of each other: today a console command produces it and a docked panel displays it, and the toolbar button that
// replaces the console later changes nothing here. Keyed by TARGET ASSET because a plan is only ever a statement about
// one questline - and stored rather than merely broadcast, so a panel opened AFTER a plan ran still finds it.

#include "CoreMinimal.h"
#include "QuestInPlacePlan.h"

class SIMPLEQUESTEDITOR_API FQuestPlanBroker
{
public:
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnPlanPublished, const FString& /*TargetAssetPath*/, const FQuestInPlacePlan& /*Plan*/);

	static FQuestPlanBroker& Get();

	/** Record a plan and tell anyone listening. Replaces any previous plan for the asset - a plan is a snapshot, not a log. */
	void Publish(const FString& TargetAssetPath, const FQuestInPlacePlan& Plan);

	/** The last plan computed for an asset, or null. */
	const FQuestInPlacePlan* Find(const FString& TargetAssetPath) const;

	/** Forget a plan. It describes a comparison against an asset state, and stops being true once that state moves. */
	void Clear(const FString& TargetAssetPath);

	FOnPlanPublished& OnPlanPublished() { return PlanPublished; }

private:
	TMap<FString, FQuestInPlacePlan> PlanByAsset;
	FOnPlanPublished PlanPublished;
};

