#!/usr/bin/env bash
# ============================================================================
# Quest Plan Scan — POSIX helper script
#
# Runs the SimpleQuest plan commandlet headlessly over a corpus of exported
# quest data. Intended for CI gating: it answers "does this data still apply
# cleanly to the assets it describes?" on every commit that touches the data.
#
# Read-only. Planning writes nothing, and there is no apply mode.
#
# Set UE_PATH below to your local Unreal install root, OR export UE_PATH in
# your shell. The script falls back to the env var if the line below is empty.
#
# Exit codes:
#   0   nothing to report under the chosen -FailOn mode
#   1   findings
#   2   the run could not complete (bad args, unreadable root, no markers
#       found, JSON write failed)
#
# Usage:
#   ./RunQuestPlanScan.sh -Root=Data/Quests
#   ./RunQuestPlanScan.sh -Root=Data/Quests -OutputJson=plan.json
#   ./RunQuestPlanScan.sh -Root=Data/Quests -FailOn=differences
#
# -Root is required and is walked RECURSIVELY. Saved/ and Intermediate/ are
# skipped. -FailOn defaults to "refusals" (fail only when a corpus CANNOT be
# applied); use "differences" when your files are the source of truth and the
# assets must always match them.
# ============================================================================

set -u

# --- Configure UE install path here OR via the UE_PATH environment variable.
UE_PATH_INLINE="/mnt/d/Program Files/UE_5.6"

if [ -z "${UE_PATH:-}" ]; then
    if [ -n "$UE_PATH_INLINE" ]; then
        UE_PATH="$UE_PATH_INLINE"
    else
        echo "ERROR: UE_PATH not set. Edit this script or export UE_PATH in your environment." >&2
        exit 2
    fi
fi

UE_EXE="$UE_PATH/Engine/Binaries/Linux/UnrealEditor-Cmd"
if [ ! -x "$UE_EXE" ]; then
    # Fall back to the Windows binary so this script is usable from Git Bash / WSL.
    UE_EXE="$UE_PATH/Engine/Binaries/Win64/UnrealEditor-Cmd.exe"
fi
if [ ! -e "$UE_EXE" ]; then
    echo "ERROR: UnrealEditor-Cmd not found under \"$UE_PATH/Engine/Binaries\"" >&2
    exit 2
fi

# Resolve project path: parent dir of Scripts/, looking for the .uproject.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
PROJECT_FILE="$(find "$PROJECT_DIR" -maxdepth 1 -name '*.uproject' | head -n 1)"

if [ -z "$PROJECT_FILE" ]; then
    echo "ERROR: No .uproject found in \"$PROJECT_DIR\"" >&2
    exit 2
fi

# Caught here as well as in the commandlet purely to save a 60-second editor
# boot on a typo. PRESENCE only — the commandlet still owns what a valid root
# or a valid -FailOn mode is, so the contract lives in one place.
if ! printf '%s\n' "$@" | grep -qi -- '-Root='; then
    echo "ERROR: -Root=<dir> is required." >&2
    echo "  e.g. ./RunQuestPlanScan.sh -Root=Data/Quests" >&2
    exit 2
fi

echo "Running QuestPlanScan commandlet"
echo "  UE:      $UE_EXE"
echo "  Project: $PROJECT_FILE"
echo "  Args:    $*"
echo

"$UE_EXE" "$PROJECT_FILE" -run=QuestPlanScan -unattended -nopause -stdout "$@"

