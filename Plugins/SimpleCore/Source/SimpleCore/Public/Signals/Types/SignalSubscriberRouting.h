// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "SignalSubscriberRouting.generated.h"

/**
 * Per-subscription routing scope. Determines which publishes this subscription receives.
 *
 *   Hierarchical (default) — receive events whose publish channel is THIS subscription's channel or any
 *                            descendant. Matches the bus's standard ancestor-walk semantic; appropriate for
 *                            "observe a scope" subscriptions like a quest-log UI watching every quest in a
 *                            namespace.
 *   Exact                  — receive events ONLY when the publishing channel exactly matches this subscription's
 *                            channel. Filters out descendant publishes; appropriate for subscriptions where
 *                            ancestor-walk delivery is noise (e.g., a Quest Giver watching its specific quest
 *                            tag doesn't care about that quest's inner Step events).
 *
 * Defaults Hierarchical so existing call sites compile unchanged with no behavior change.
 */
UENUM(BlueprintType)
enum class ESignalSubscriberRouting : uint8
{
	Hierarchical UMETA(DisplayName = "Hierarchical", Tooltip = "Receive events on this channel or any descendant (via the bus's ancestor walk)."),
	Exact        UMETA(DisplayName = "Exact Match",   Tooltip = "Receive events only when the publish channel exactly matches this subscription's channel."),
};