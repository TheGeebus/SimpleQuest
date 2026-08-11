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
 * WHICH PROVENANCE a source is. Lives beside FQuestPlanSource because that is what it describes; the resolver's private
 * FQuestDataEndpoint derives its own kind from the same fact, through one helper, so the two cannot disagree.
 */
enum class EQuestPlanSourceKind : uint8
{
	Folder,
	DataTable,
};

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
	FSoftObjectPath Mapping; // the translation mapping in force, invalid for none	

	/**
	 * ONE derivation of the kind, so no caller re-implements "a source naming a table is a table source". Note this is
	 * a statement about a RECORD, which always has one or the other; a UI may legitimately sit on DataTable with
	 * nothing picked yet, which is a state no record can hold and which the panel therefore tracks itself.
	 */
	EQuestPlanSourceKind Kind() const { return Table.IsValid() ? EQuestPlanSourceKind::DataTable : EQuestPlanSourceKind::Folder; }


	bool IsValid() const { return !Folder.IsEmpty() || Table.IsValid(); }
};

struct FQuestPlanRecord
{
	FQuestInPlacePlan Plan;
	FQuestPlanSource Source;
	
	/**
	 * Non-empty when the last attempt on this asset FAILED TO PRODUCE A PLAN at all - an unreadable source, a refused
	 * mapping, an incoherent bundle. Distinct from a plan that found nothing: one says the questline matches its
	 * source, the other says we never got far enough to know. Plan is left at its previous value and must not be read
	 * while this is set.
	 */
	FString Error;
};

class SIMPLEQUESTEDITOR_API FQuestPlanBroker
{
public:
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnPlanPublished, const FString& /*TargetAssetPath*/, const FQuestInPlacePlan& /*Plan*/);

	static FQuestPlanBroker& Get();

	/** Record a plan and what produced it, then tell anyone listening. Replaces any previous plan for the asset. */
	void Publish(const FString& TargetAssetPath, const FQuestInPlacePlan& Plan, const FQuestPlanSource& Source);

	/**
	 * Record that an attempt produced NO plan, and why. Goes through the same channel for the same reason a plan does:
	 * a producer that fails and a producer that succeeds should not need different consumers, and a panel that shows
	 * "no plan yet" when the truth is "the source could not be read" is telling a designer to try the thing that just
	 * failed. Reason reaches every listener, so a console failure surfaces in the panel too.
	 */
	void PublishFailure(const FString& TargetAssetPath, const FString& Reason, const FQuestPlanSource& Source);

	/** The last plan computed for an asset, with its source, or null. */
	const FQuestPlanRecord* Find(const FString& TargetAssetPath) const;

	/** Forget a plan. It describes a comparison against an asset state, and stops being true once that state moves. */
	void Clear(const FString& TargetAssetPath);

	FOnPlanPublished& OnPlanPublished() { return PlanPublished; }

private:
	TMap<FString, FQuestPlanRecord> RecordByAsset;
	FOnPlanPublished PlanPublished;
};