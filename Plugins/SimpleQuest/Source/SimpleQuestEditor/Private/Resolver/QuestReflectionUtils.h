// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

// Shared reflection helpers for the resolver: the single predicate that decides whether a property is authored config
// (so export, the mapping transform, and the mapping picker all agree on the exact same property set), plus the small
// queries the mapping needs to bind a source column onto a target class's property.

#include "CoreMinimal.h"

/**
 * True when Prop counts as authored config: designer-editable (CPF_Edit) OR explicitly opted in via meta=(QuestExport)
 * — the marker for authored state mutated through custom actions rather than the Details panel (e.g. combinator
 * ConditionPinCount). EditConst (VisibleAnywhere) and Transient are never authored config. This is the one predicate
 * every resolver surface shares; a divergent copy would let export, apply, and the picker disagree on what's mappable.
 */
bool IsAuthoredConfigProperty(const FProperty* Prop);

/**
 * The authored-config property names on a class — the mapping picker's target list, and the answer to "does this class
 * have a property named X" for the bind-where-it-fits rule. Runs the same field walk export uses, filtered by
 * IsAuthoredConfigProperty.
 */
TArray<FName> GetAuthoredPropertyNames(const UClass* Class);
