// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once
#include "NativeGameplayTags.h"

/**
 * Reserved reward-blocker kinds. A preview carries these rather than being suppressed, so a UI can branch on WHY a
 * reward is unavailable - an icon, a color, a "come back later" affordance - instead of parsing the description.
 * Adopters add their own under SimpleQuest.RewardBlocker for modifiers they write.
 */
SIMPLEQUEST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_RewardBlocker_AlreadyGranted);
SIMPLEQUEST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_RewardBlocker_MissingFact);
SIMPLEQUEST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_RewardBlocker_CompletionCount);
SIMPLEQUEST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_RewardBlocker_NoValue);

