#!/usr/bin/env bash
# =============================================================================
# Stale Quest Tags Scan — *nix helper script
#
# Runs the SimpleQuest stale-tag commandlet headlessly. Intended for pre-tag-
# consolidation validation, pre-release sweeps, or CI gating.
#
# Set UE_PATH below to your local Unreal install root, OR export UE_PATH in
# your shell. The script falls back to the env var if the line below is empty.
#
# Exit codes:
#   0   no stale references found
#   1   one or more stale references found
#   2   commandlet failed (could not init, JSON write failed, etc.)
#
# Usage:
#   ./RunStaleQuestTagsScan.sh                          (log output only)
#   ./RunStaleQuestTagsScan.sh -OutputJson=results.json (also write JSON)
#   ./RunStaleQuestTagsScan.sh -FastWP                  (class-filter WP scan)
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

echo "Running StaleQuestTagsScan commandlet"
echo "  UE:      $UE_EXE"
echo "  Project: $PROJECT_FILE"
echo "  Args:    $*"
echo

"$UE_EXE" "$PROJECT_FILE" -run=StaleQuestTagsScan -unattended -nopause -stdout "$@"