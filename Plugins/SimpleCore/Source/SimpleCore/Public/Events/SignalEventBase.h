// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include <concepts>

#include "SignalEventBase.generated.h"

USTRUCT(BlueprintType)
struct FSignalEventBase
{
	GENERATED_BODY()

	FSignalEventBase() = default;

};

/**
 * Concept satisfied by any struct deriving from FSignalEventBase. Use as a template-parameter constraint
 * (`template<CSignalEvent TEvent>` or `requires CSignalEvent<TEvent>`) wherever a public API needs to accept
 * "any Simple Suite event" without admitting unrelated types with compile time enforcement that generates readable errors.
 */
template<typename T>
concept CSignalEvent = std::derived_from<T, FSignalEventBase>;