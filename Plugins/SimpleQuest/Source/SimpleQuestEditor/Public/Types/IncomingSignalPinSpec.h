// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "IncomingSignalPinSpec.generated.h"

/**
 * Describes one incoming outcome signal exposed as a pin on an Entry node's output side.
 *
 * The Entry node maintains an array of these specs. Each spec with bExposed=true generates a visible output pin whose
 * category/name are derived from Outcome. Unexposed specs persist in the array (so the designer's selection state is
 * preserved across re-imports) but do not generate pins.
 *
 * Source identity uses FSoftObjectPath + FGuid so specs remain stable across asset renames and recompiles. ParentAsset
 * is empty when the Entry is inside a Quest node's inline inner graph — the parent graph shares the Entry's asset, so no
 * asset-level qualifier is needed.
 *
 * Session 19 introduces this struct with flat (unqualified) entries to replace the legacy IncomingOutcomeTags array.
 * Session 20+ adds source-node qualification (SourceNodeGuid + ParentAsset populated) when the import utility and details
 * panel rewrite land.
 */
USTRUCT()
struct SIMPLEQUESTEDITOR_API FIncomingSignalPinSpec
{
	GENERATED_BODY()

	/**
	 * Asset containing the source node, when the source lives in a different asset than the Entry node. Empty for Entry
	 * inside an inline Quest inner graph — the parent graph shares the Entry's asset.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Incoming Signal")
	FSoftObjectPath ParentAsset;

	/**
	 * Persistent GUID of the specific source content node whose outcome this spec represents. Invalid GUID means this spec
	 * is not yet qualified to a specific source (legacy-migrated or Session 19 import).
	 */
	UPROPERTY(VisibleAnywhere, Category = "Incoming Signal")
	FGuid SourceNodeGuid;

	/**
	 * The specific outcome tag this spec represents. Invalid tag means "any outcome from the source node" — the source's
	 * Any Outcome pin.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Incoming Signal", meta = (Categories = "Quest.Outcome"))
	FGameplayTag Outcome;

	/**
	 * When true, generate a corresponding output pin on the Entry node. Designer toggle via the details panel. Defaults
	 * to false so imports are non-destructive — the designer opts in.
	 */
	UPROPERTY(EditAnywhere, Category = "Incoming Signal")
	bool bExposed = false;
};