// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

// Where a computed plan waits for whoever wants to show or run it. What PRODUCES a plan and what CONSUMES one are
// deliberately unaware of each other: today a console command and a toolbar button produce, a docked panel and a
// toolbar action consume. Keyed by TARGET ASSET because a plan is only ever a statement about one questline, and
// STORED rather than merely broadcast, so a consumer arriving later still finds it.

#include "CoreMinimal.h"
#include "QuestInPlacePlan.h"
#include "UObject/SoftObjectPath.h"

/**
 * Where a plan's data came from, as plain fields rather than an FQuestDataEndpoint - that type is private to the
 * resolver and this header is public. Carried WITH the plan because applying re-reads the source, so a consumer that
 * can see a plan must also be able to name what produced it; otherwise the surface showing a plan and the surface
 * offering to apply it disagree about whether there is anything to do.
 */
struct FQuestPlanSource
{
	FString Folder;          // empty when the source was a DataTable
	FString FormatName;      // meaningful only for a folder
	FSoftObjectPath Table;   // invalid when the source was a folder

	bool IsValid() const { return !Folder.IsEmpty() || Table.IsValid(); }
};

struct FQuestPlanRecord
{
	FQuestInPlacePlan Plan;
	FQuestPlanSource Source;
};

class SIMPLEQUESTEDITOR_API FQuestPlanBroker
{
public:
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnPlanPublished, const FString& /*TargetAssetPath*/, const FQuestInPlacePlan& /*Plan*/);

	static FQuestPlanBroker& Get();

	/** Record a plan and what produced it, then tell anyone listening. Replaces any previous plan for the asset. */
	void Publish(const FString& TargetAssetPath, const FQuestInPlacePlan& Plan, const FQuestPlanSource& Source);

	/** The last plan computed for an asset, with its source, or null. */
	const FQuestPlanRecord* Find(const FString& TargetAssetPath) const;

	/** Forget a plan. It describes a comparison against an asset state, and stops being true once that state moves. */
	void Clear(const FString& TargetAssetPath);

	FOnPlanPublished& OnPlanPublished() { return PlanPublished; }

private:
	TMap<FString, FQuestPlanRecord> RecordByAsset;
	FOnPlanPublished PlanPublished;
};