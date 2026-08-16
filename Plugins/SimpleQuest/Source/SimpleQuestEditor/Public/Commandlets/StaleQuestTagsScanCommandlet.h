// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "StaleQuestTagsScanCommandlet.generated.h"

/**
 * Headless project-wide scan for stale quest-tag references on giver / target / observer components.
 * Mirrors the Stale Quest Tags panel's "Full Project Scan" button — calls the same backend
 * (FSimpleQuestEditorUtilities::CollectStaleQuestTagEntries) with all scope bits enabled and
 * comprehensive WP coverage.
 *
 * Designed for invocation outside a designer session — pre-tag-namespace-consolidation sweeps,
 * pre-release validation, or CI gating in studios that want to fail a build on tag drift.
 *
 * Invocation:
 *   UnrealEditor-Cmd.exe <Project>.uproject -run=StaleQuestTagsScan [-OutputJson=path] [-FastWP]
 *
 * Args:
 *   -OutputJson=<path>  Write structured results to a JSON file in addition to log output.
 *                       Path is interpreted relative to the project directory if not absolute.
 *   -FastWP             Use the class-filter optimization for World Partition scans (skips actors
 *                       whose class isn't known to author quest components). Faster on large WP
 *                       projects but misses per-instance component additions; default is
 *                       comprehensive scan.
 *
 * Output:
 *   - Log: per-entry Warning lines + a Display summary tagged StaleQuestTagsScan:
 *   - Stdout/stderr: same as log when LogToConsole is set
 *   - JSON file (optional): array of entries + per-source counts
 *
 * Exit code:
 *   0   no stale references found
 *   1   one or more stale references found
 *   2   the scan could not complete (could not initialize, JSON write failed)
 *
 * Those three are a CONTRACT, shared with the wrapper scripts in Scripts/ and with every other SimpleQuest
 * commandlet: 0 clean, 1 findings, 2 the run itself failed. A negative code is deliberately not used - a process
 * exiting -1 is reported as 255 by both cmd and bash, so it would reach a build server as a number appearing in no
 * documentation, and it collides with nothing a caller could switch on.
 */
UCLASS()
class UStaleQuestTagsScanCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UStaleQuestTagsScanCommandlet();

	virtual int32 Main(const FString& Params) override;
};