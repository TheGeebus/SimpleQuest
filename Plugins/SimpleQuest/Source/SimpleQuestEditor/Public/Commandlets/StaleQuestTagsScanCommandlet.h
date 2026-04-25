// Copyright 2026, Greg Bussell, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "StaleQuestTagsScanCommandlet.generated.h"

/**
 * Headless project-wide scan for stale quest-tag references on giver / target / watcher components.
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
 *  <0   unexpected failure (could not initialize, write JSON, etc.)
 */
UCLASS()
class UStaleQuestTagsScanCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UStaleQuestTagsScanCommandlet();

	virtual int32 Main(const FString& Params) override;
};