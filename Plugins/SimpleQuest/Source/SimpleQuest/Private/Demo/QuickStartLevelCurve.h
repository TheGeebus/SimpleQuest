// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Curves/CurveFloat.h"
#include "Engine/DataAsset.h"
#include "QuickStartLevelCurve.generated.h"

/**
 * The QuickStart's progression curve: how much cumulative experience each level costs. The HUD reads its bar
 * maximum from here rather than from a constant, and whatever grants experience asks it whether a grant crossed
 * a threshold instead of comparing against a hardcoded number.
 *
 * Nothing stores the player's level. It is derived from the running total every time it is asked for, so retuning
 * the curve re-levels an existing save correctly instead of leaving a saved number the curve no longer agrees with.
 *
 * To fire a level-up: compare GetLevelForTotal before and after adding a grant.
 */
UCLASS(BlueprintType)
class UQuickStartLevelCurve : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/**
	 * X is the level, Y is the CUMULATIVE experience needed to reach it. Level 1 sits at 0. Inline rather than a
	 * separate curve asset so the whole progression is editable in one place.
	 *
	 * *** THE LAST KEY IS NOT A FREE NUMBER: it must equal the sum of every chapter's experience reward, *** which
	 * is what makes the bar finish exactly full on the final chapter. Retuning a chapter's XP means moving it.
	 *
	 * The QuickStart authors exactly two keys, because the tutorial has exactly one level-up in it - reaching the
	 * second level IS the ending. Adding keys extends the curve and needs no code change.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Progression", meta = (XAxisName = "Level", YAxisName = "Total Experience"))
	FRuntimeFloatCurve LevelCurve;

	/** Highest level the curve defines, taken from its last key so there is no second number to keep in sync. */
	UFUNCTION(BlueprintPure, Category = "Progression")
	int32 GetMaxLevel() const;

	/** 1-based level for a running total. Clamps to GetMaxLevel. */
	UFUNCTION(BlueprintPure, Category = "Progression")
	int32 GetLevelForTotal(int32 ExperienceTotal) const;

	/**
	 * The bar's current value: experience earned into this level, never the running total. Always measured from
	 * zero at this level's threshold. At max level this equals GetLevelSpan, so the bar reads full and holds.
	 */
	UFUNCTION(BlueprintPure, Category = "Progression")
	int32 GetProgressIntoLevel(int32 ExperienceTotal) const;

	/**
	 * The bar's maximum: what this level spans, next threshold minus this one. Never zero, so a percentage is
	 * always safe to compute.
	 */
	UFUNCTION(BlueprintPure, Category = "Progression")
	int32 GetLevelSpan(int32 ExperienceTotal) const;

private:
	/** Curve evaluation rounded to whole experience - the curve is float, experience is not. */
	int32 ThresholdForLevel(int32 Level) const;

	/** One walk of the curve: the level a total sits in, and the band the bar measures against. */
	void ResolveBand(int32 ExperienceTotal, int32& OutLevel, int32& OutFloor, int32& OutCeiling) const;
};

