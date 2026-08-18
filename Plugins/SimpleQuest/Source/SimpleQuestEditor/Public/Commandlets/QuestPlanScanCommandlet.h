// Copyright (c) 2026 Greg Bussell
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "QuestPlanScanCommandlet.generated.h"

/**
 * Headless plan run over a corpus of exported quest data - the CI gate for the data resolver.
 *
 * A studio keeping progression data in files outside the .uassets wants to know, on every commit that touches those
 * files, whether the data still applies cleanly to the assets it describes. That question IS a plan run. This walks a
 * root, finds every folder that declares itself with a SimpleQuest export marker, plans each one against the questline
 * its marker names, and reports the lot as structured JSON plus a CI-friendly exit code.
 *
 * READ-ONLY BY CONSTRUCTION. Planning writes nothing, so there is no --apply here and there should not be one.
 *
 * ONE EDITOR BOOT, MANY PLANS. An editor start costs 30-60 seconds, so a per-questline invocation would make a
 * twenty-questline project a twenty-minute build step - which is a gate nobody keeps. The unit of work is the corpus.
 *
 * Invocation:
 *   UnrealEditor-Cmd.exe <Project>.uproject -run=QuestPlanScan -Root=<dir> [-OutputJson=path] [-FailOn=refusals|differences]
 *
 * Args:
 *   -Root=<dir>         REQUIRED. Walked RECURSIVELY for export markers. Relative paths resolve against the project
 *                       directory. Saved/ and Intermediate/ are skipped - engine scratch, and an export probe living
 *                       there should not gate anybody's build.
 *   -OutputJson=<path>  Write the run as JSON in addition to the log. Relative paths resolve against the project dir.
 *   -FailOn=<mode>      What counts as a build failure. "refusals" (default) fails only when a corpus CANNOT be
 *                       applied. "differences" also fails when a corpus and its asset merely disagree - right for a
 *                       studio whose files are the source of truth and whose assets must always match them.
 *
 * Exit code - the contract shared with StaleQuestTagsScan and the wrapper scripts in Scripts/:
 *   0   nothing to report under the chosen -FailOn mode
 *   1   findings
 *   2   the run itself could not complete (no -Root, unreadable root, no markers found, JSON write failed)
 *
 * A RUN THAT FINDS NO MARKERS EXITS 2, NOT 0. A gate that checked nothing must never report success - that is how a
 * mistyped root becomes a green build for a month.
 */
UCLASS()
class UQuestPlanScanCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UQuestPlanScanCommandlet();

	virtual int32 Main(const FString& Params) override;
};

