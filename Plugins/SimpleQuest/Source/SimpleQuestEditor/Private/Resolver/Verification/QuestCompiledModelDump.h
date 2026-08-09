// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

// Renders a questline's COMPILED model as deterministic text, so two assets can be compared on BEHAVIOUR rather than on
// authorship. It reads GetCompiledNodes and a compiler-stamped property allowlist, never the authored graph - and every
// render DELIBERATELY DESTROYS incidental identity: loc namespaces, container order, commutative combinator child order,
// object paths. Destroying information would be a bug in a pipeline stage; here it is exactly the point, because two
// assets that behave identically must produce identical text or the comparison reports differences that mean nothing.

#include "CoreMinimal.h"

class UQuestlineGraph;

/**
 * Render Graph's compiled model as one line per (scope, field, value), graph-level properties first, then one block per
 * compiled node with tags sorted and properties in declaration order.
 * OutNodeCount receives how many compiled nodes were visited. It comes back separately because the dump FILE never
 * carries that number - only the caller's log line does - so returning just the lines would lose it.
 */
TArray<FString> RenderQuestCompiledModel(const UQuestlineGraph& Graph, int32& OutNodeCount);

