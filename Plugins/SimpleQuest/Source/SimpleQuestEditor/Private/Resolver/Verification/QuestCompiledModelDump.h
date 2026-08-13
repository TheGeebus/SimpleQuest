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
 * OutNodeCount receives how many compiled nodes were visited, for the caller's log line. The count is ALSO emitted
 * into the dump as a GRAPH line, because a comparison that cannot see it cannot catch a node-count change that
 * happens to normalize away everywhere else.
 */
TArray<FString> RenderQuestCompiledModel(const UQuestlineGraph& Graph, int32& OutNodeCount);

