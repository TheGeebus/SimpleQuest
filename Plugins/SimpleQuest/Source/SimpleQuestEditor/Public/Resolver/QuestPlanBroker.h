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
 * WHICH PROVENANCE a source is. Lives beside FQuestPlanEndpoint because that is what it describes; the resolver's private
 * FQuestDataEndpoint derives its own kind from the same fact, through one helper, so the two cannot disagree.
 */
enum class EQuestPlanEndpointKind : uint8
{
	Folder,
	DataTable,
};

/**
 * An ENDPOINT a plan concerns: a folder plus its format, or a DataTable, plus the translation mapping in force. Held as
 * plain fields rather than the resolver's own FQuestDataEndpoint because that type is private and this header is public.
 *
 * DIRECTION-NEUTRAL ON PURPOSE. The same shape describes where data is READ FROM and where it is WRITTEN INTO; which of
 * those it means is carried by the member holding it and by the plan's own Direction, never by this type. Reading a
 * direction into the type itself is a mistake it cannot catch for you.
 */
struct FQuestPlanEndpoint
{
	FString Folder;          // empty when this endpoint is a DataTable
	FString FormatName;      // meaningful only for a folder
	FSoftObjectPath Table;   // invalid when this endpoint is a folder
	FSoftObjectPath Mapping; // the translation mapping in force, invalid for none	

	/**
	 * ONE derivation of the kind, so no caller re-implements "a source naming a table is a table source". Note this is
	 * a statement about a RECORD, which always has one or the other; a UI may legitimately sit on DataTable with
	 * nothing picked yet, which is a state no record can hold and which the panel therefore tracks itself.
	 */
	EQuestPlanEndpointKind Kind() const { return Table.IsValid() ? EQuestPlanEndpointKind::DataTable : EQuestPlanEndpointKind::Folder; }


	bool IsValid() const { return !Folder.IsEmpty() || Table.IsValid(); }
};

struct FQuestPlanRecord
{
	FQuestInPlacePlan Plan;
	
	/**
	 * WHAT THIS PLAN WAS BUILT FROM - provenance, not a direction. For an inbound plan that is the source endpoint; for
	 * an OUTBOUND one it is the destination, because that is what an export plan was computed against. Reading it as
	 * "the source endpoint" is what made every export plan compare against the import row and report itself stale on
	 * the instant it was built.
	 */
	FQuestPlanEndpoint Provenance;
	
	/**
	 * Non-empty when the last attempt on this asset FAILED TO PRODUCE A PLAN at all - an unreadable source, a refused
	 * mapping, an incoherent bundle. Distinct from a plan that found nothing: one says the questline matches its
	 * source, the other says we never got far enough to know. Plan is left at its previous value and must not be read
	 * while this is set.
	 */
	FString Error;
};

DECLARE_MULTICAST_DELEGATE_TwoParams(FOnPlanPublished, const FString& /*TargetAssetPath*/, const FQuestInPlacePlan& /*Plan*/);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnExportCompleted, const FString& /*TargetAssetPath*/, const FString& /*Summary*/, const FString& /*Error*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnPlanCleared, const FString& /*TargetAssetPath*/);

class SIMPLEQUESTEDITOR_API FQuestPlanBroker
{
	
public:
	static FQuestPlanBroker& Get();

	/** Record a plan and what produced it, then tell anyone listening. Replaces any previous plan for the asset. */
	void Publish(const FString& TargetAssetPath, const FQuestInPlacePlan& Plan, const FQuestPlanEndpoint& Provenance);

	/**
	 * Record that an attempt produced NO plan, and why. Goes through the same channel for the same reason a plan does:
	 * a producer that fails and a producer that succeeds should not need different consumers, and a panel that shows
	 * "no plan yet" when the truth is "the source could not be read" is telling a designer to try the thing that just
	 * failed. Reason reaches every listener, so a console failure surfaces in the panel too.
	 */
	void PublishFailure(const FString& TargetAssetPath, const FString& Reason, const FQuestPlanEndpoint& Provenance);

	/** The last plan computed for an asset, with its source, or null. */
	const FQuestPlanRecord* Find(const FString& TargetAssetPath) const;
	
	/**
	 * Forget a plan, AND SAY SO. A plan describes a comparison against a particular state, so applying it turns it into
	 * a description of work already done - and a stored plan that has been executed is worse than none, because it
	 * stays applyable: a second press re-plans, finds nothing, and writes nothing, while the button looked live.
	 */
	void Clear(const FString& TargetAssetPath);

	FOnPlanCleared& OnPlanCleared() { return PlanCleared; }

	FOnPlanPublished& OnPlanPublished() { return PlanPublished; }


	/**
	 * Report an export attempt. Exactly one of Summary and Error is non-empty.
	 * BROADCAST ONLY, deliberately unlike a plan - and the asymmetry is the point rather than an oversight. A plan is
	 * REVIEWED THEN ACTED ON, so it is stored and a consumer arriving late still finds it; an export receipt is
	 * consumed by whoever asked for it, and "exported forty minutes ago" is stale noise on a panel. It goes through the
	 * broker at all so a CONSOLE refusal reaches an open panel, which is the same reason plan failures do.
	 */
	void PublishExport(const FString& TargetAssetPath, const FString& Summary, const FString& Error);

	FOnExportCompleted& OnExportCompleted() { return ExportCompleted; }

private:
	TMap<FString, FQuestPlanRecord> RecordByAsset;
	FOnPlanCleared PlanCleared;
	FOnPlanPublished PlanPublished;
	FOnExportCompleted ExportCompleted;
};