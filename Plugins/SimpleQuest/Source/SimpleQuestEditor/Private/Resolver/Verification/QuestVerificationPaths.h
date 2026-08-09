// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

// Shared constants for the verification tier. Kept apart from the oracle itself because the console commands and the
// comparators both need them, and a copy in each is how the two drift.

#include "CoreMinimal.h"

/**
 * Suffix the round-trip harness gives its reconstructed asset, so the copy compiles into its own tag namespace instead
 * of colliding with the original. Every comparison normalizes it away before diffing.
 */
inline constexpr const TCHAR* GQuestRoundTripSuffix = TEXT("_RT");

