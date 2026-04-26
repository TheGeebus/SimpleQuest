// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "IncomingSignalPinSpec.generated.h"

/**
 * Describes one incoming outcome signal exposed as a pin on an Entry node's output side.
 *
 * Each spec is fully qualified: it identifies the exact (source node, outcome, containing asset) triple whose firing
 * should activate the corresponding entry pin. Unqualified specs (missing SourceNodeGuid) are invalid state and will
 * be scrubbed by PostLoad and skipped by the compiler.
 *
 * Specs with bExposed=true generate visible output pins; unexposed specs persist in the array so the designer's
 * selection state survives re-imports but do not generate pins. ParentAsset is empty when the source node lives in
 * the same asset as this Entry (inline Quest inner graph case). CachedSourceLabel captures the source node's display
 * label at Import time, cached for disambiguation without needing to re-resolve the source node during pin allocation.
 */
USTRUCT()
struct SIMPLEQUESTEDITOR_API FIncomingSignalPinSpec
{
	GENERATED_BODY()

	/**
	 * Asset containing the source node, when the source lives in a different asset than the Entry. Empty for
	 * same-asset sources (inline Quest inner graph).
	 */
	UPROPERTY(VisibleAnywhere, Category = "Incoming Signal")
	FSoftObjectPath ParentAsset;

	/**
	 * Persistent GUID of the specific source content node whose outcome this spec represents. Must be valid —
	 * unqualified specs are invalid state.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Incoming Signal")
	FGuid SourceNodeGuid;

	/**
	 * Display label of the source node, captured at Import time. Used for pin-name disambiguation without requiring
	 * the parent asset to be loaded at pin-allocation time. Re-run Import to refresh if the source node is renamed.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Incoming Signal")
	FString CachedSourceLabel;

	/** The specific outcome tag this spec represents. Must be valid. */
	UPROPERTY(VisibleAnywhere, Category = "Incoming Signal", meta = (Categories = "SimpleQuest.QuestOutcome"))
	FGameplayTag Outcome;

	/**
	 * When true, generate a corresponding output pin on the Entry node. Designer toggle via the details panel.
	 * Defaults to false so imports are non-destructive — the designer opts in.
	 */
	UPROPERTY(EditAnywhere, Category = "Incoming Signal")
	bool bExposed = false;
};