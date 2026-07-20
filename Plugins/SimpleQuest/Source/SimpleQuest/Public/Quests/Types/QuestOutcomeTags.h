// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once
#include "NativeGameplayTags.h"

/**
 * Reserved "fires on completion regardless of outcome" tag — the any-outcome case as a real, keyable tag (the runtime
 * any-outcome routing is NAME_None; this is its authoring-side tag identity, e.g. as a QuestlineRewards map key).
 */
SIMPLEQUEST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Outcome_AnyOutcome);