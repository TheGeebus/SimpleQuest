#!/usr/bin/env bash
# =============================================================================
# Automation Tests — *nix helper script
#
# Runs every SimpleQuest and SimpleCore automation test headlessly: no editor
# window and no rendering. Intended for pre-release sweeps and CI gating, and
# just as useful by hand after a change.
#
# Set UE_PATH below to your local Unreal install root, OR export UE_PATH in
# your shell. The environment variable wins when both are set.
#
# The report (index.json and index.html) always goes to
# Saved/Automation/Headless under the project. That folder is deleted at the
# start of every run, so a report found there always belongs to the last run.
# Scripts/CheckTestCount.ps1 reads it.
#
# Exit codes (the same contract as the other wrappers in Scripts/):
#   0   every test passed
#   1   the tests ran and at least one failed
#   2   the run could not finish: this script could not find the engine or
#       the project, the editor crashed or failed to start, no test matched
#       the filter, or the test runner found no worker to run on
#
# Usage:
#   ./RunAutomationTests.sh
#   ./RunAutomationTests.sh <extra editor arguments, passed through unchanged>
# =============================================================================

set -e

# --- Configure UE install path here OR via the UE_PATH environment variable.
UE_PATH_INLINE=""

if [ -z "${UE_PATH:-}" ]; then
    if [ -n "$UE_PATH_INLINE" ]; then
        UE_PATH="$UE_PATH_INLINE"
    else
        echo "ERROR: UE_PATH not set. Edit this script or export UE_PATH in your environment." >&2
        exit 2
    fi
fi

# Detect platform-specific binary location.
case "$(uname -s)" in
    Linux*)  UE_EXE="$UE_PATH/Engine/Binaries/Linux/UnrealEditor-Cmd" ;;
    Darwin*) UE_EXE="$UE_PATH/Engine/Binaries/Mac/UnrealEditor-Cmd" ;;
    *)       echo "ERROR: unsupported platform $(uname -s)" >&2; exit 2 ;;
esac

if [ ! -x "$UE_EXE" ]; then
    echo "ERROR: UnrealEditor-Cmd not found or not executable at \"$UE_EXE\"" >&2
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

# The engine resolves a relative report path against its own binaries folder, not the project's, so the report
# folder is built as a full path.
REPORT_DIR="$PROJECT_DIR/Saved/Automation/Headless"

# Deleted first so a run that dies before writing its report leaves NO report, rather than the last run's passing one.
rm -rf "$REPORT_DIR"

# Prefix matches on the test name, joined with "+". "StartsWith:SimpleQuest" matches names beginning "SimpleQuest.",
# so a test elsewhere that merely mentions SimpleQuest in its name is never swept in.
TEST_FILTER="StartsWith:SimpleQuest+StartsWith:SimpleCore"

echo "Running automation tests"
echo "  UE:      $UE_EXE"
echo "  Project: $PROJECT_FILE"
echo "  Filter:  $TEST_FILTER"
echo "  Report:  $REPORT_DIR"
echo "  Args:    $*"
echo

# "|| UE_EXIT=$?" catches the editor's failure code, which set -e would otherwise turn into an immediate exit before it
# could be translated below.
UE_EXIT=0
"$UE_EXE" "$PROJECT_FILE" -ExecCmds="Automation RunTests $TEST_FILTER;Quit" -ReportExportPath="$REPORT_DIR" -unattended -nopause -nosplash -NullRHI -stdout "$@" || UE_EXIT=$?

# The editor's own codes, turned into this script's. The engine logs "EXIT CODE: -1" when a test fails, when no test
# matches the filter, and when it finds no automation worker, but its exit call takes a single byte, so the process
# really exits 255 for all three. Only a run whose tests actually ran writes a report, and the report folder was
# emptied above, so 255 WITH a report means a test failed. Anything else means the editor stopped first.
case "$UE_EXIT" in
    0)   exit 0 ;;
    255) if [ -f "$REPORT_DIR/index.json" ]; then
             echo "Tests failed: the editor exited 255, which its log prints as \"EXIT CODE: -1\"."
             exit 1
         fi
         echo "ERROR: the run failed before any test ran: no test matched the filter, or no automation worker was found." >&2
         echo "  The editor exited 255 and wrote no report. Search the log for \"No automation tests matched\" or \"Failed to find workers\"." >&2
         exit 2 ;;
    *)   echo "ERROR: the editor exited with code $UE_EXIT before the tests finished. Read the newest log in Saved/Logs." >&2
         exit 2 ;;
esac
