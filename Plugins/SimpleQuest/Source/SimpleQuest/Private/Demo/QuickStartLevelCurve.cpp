// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#include "QuickStartLevelCurve.h"

int32 UQuickStartLevelCurve::ThresholdForLevel(int32 Level) const
{
	const FRichCurve* Curve = LevelCurve.GetRichCurveConst();
	return Curve ? FMath::RoundToInt(Curve->Eval(static_cast<float>(Level))) : 0;
}

int32 UQuickStartLevelCurve::GetMaxLevel() const
{
	const FRichCurve* Curve = LevelCurve.GetRichCurveConst();
	if (!Curve || Curve->GetNumKeys() == 0) return 1;

	float MinTime = 0.f, MaxTime = 0.f;
	Curve->GetTimeRange(MinTime, MaxTime);
	return FMath::Max(1, FMath::RoundToInt(MaxTime));
}

void UQuickStartLevelCurve::ResolveBand(int32 ExperienceTotal, int32& OutLevel, int32& OutFloor, int32& OutCeiling) const
{
	const int32 MaxLevel = GetMaxLevel();

	// Inverting the curve: step up while the NEXT level is already paid for. Bounded by the curve's own last key,
	// so an oddly authored curve cannot send this walking forever.
	OutLevel = 1;
	while (OutLevel < MaxLevel && ExperienceTotal >= ThresholdForLevel(OutLevel + 1))
	{
		++OutLevel;
	}

	// The band the bar measures: zero at this level's threshold, full at the next. At max level there is no next
	// threshold, so the last band is reused and the bar reads full and holds - which is what sits behind the
	// level-up widget rather than a bar that empties at the moment of the payoff.
	const bool bAtMax = OutLevel >= MaxLevel;
	OutFloor   = ThresholdForLevel(bAtMax ? FMath::Max(1, MaxLevel - 1) : OutLevel);
	OutCeiling = ThresholdForLevel(bAtMax ? MaxLevel : OutLevel + 1);
}

int32 UQuickStartLevelCurve::GetLevelForTotal(int32 ExperienceTotal) const
{
	int32 Level = 1, Floor = 0, Ceiling = 0;
	ResolveBand(ExperienceTotal, Level, Floor, Ceiling);
	return Level;
}

int32 UQuickStartLevelCurve::GetLevelSpan(int32 ExperienceTotal) const
{
	int32 Level = 1, Floor = 0, Ceiling = 0;
	ResolveBand(ExperienceTotal, Level, Floor, Ceiling);
	return FMath::Max(1, Ceiling - Floor);
}

int32 UQuickStartLevelCurve::GetProgressIntoLevel(int32 ExperienceTotal) const
{
	int32 Level = 1, Floor = 0, Ceiling = 0;
	ResolveBand(ExperienceTotal, Level, Floor, Ceiling);

	const int32 Span = FMath::Max(1, Ceiling - Floor);
	if (Level >= GetMaxLevel())
	{
		return Span;   // finished: full and held
	}
	return FMath::Clamp(ExperienceTotal - Floor, 0, Span);
}

