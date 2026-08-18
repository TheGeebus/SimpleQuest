// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

// Turns a TYPED UPROPERTY value into a neutral bundle cell. Shared by every provenance that reads from live reflection data
// rather than from text: the graph export walks node properties, the Data Table source walks row-struct properties. Both
// have the FProperty in hand, so both can emit a properly-Kinded cell (Tag / Enum / Reference / Array / StructLiteral)
// instead of degrading to a string — that degradation is only forced on TEXT providers, which have nothing but text.

#include "CoreMinimal.h"
#include "QuestDataValue.h"

/**
 * Build a bundle cell from a property value.
 * @param Prop       The reflected property describing the value's domain type.
 * @param ValuePtr   Address of the value itself (NOT the container).
 * @param DefaultPtr Address of the comparison default, or null to skip the check. When non-null and the value compares
 *                   Identical to it, the result is Kind::Empty — the "absent == at-default" contract every consumer reads.
 */
FQuestDataValue BuildQuestDataValue(const FProperty* Prop, const void* ValuePtr, const void* DefaultPtr);