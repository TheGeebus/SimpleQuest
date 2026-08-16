@echo off
REM ============================================================================
REM Quest Plan Scan — Windows helper script
REM
REM Runs the SimpleQuest plan commandlet headlessly over a corpus of exported
REM quest data. Intended for CI gating: it answers "does this data still apply
REM cleanly to the assets it describes?" on every commit that touches the data.
REM
REM Read-only. Planning writes nothing, and there is no apply mode.
REM
REM Set UE_PATH below to your local Unreal install root, OR set the UE_PATH
REM environment variable in your shell. The script falls back to the env var if
REM the line below is left empty.
REM
REM Exit codes:
REM   0   nothing to report under the chosen -FailOn mode
REM   1   findings
REM   2   the run could not complete (bad args, unreadable root, no markers
REM       found, JSON write failed)
REM
REM Usage:
REM   RunQuestPlanScan.bat -Root=Data\Quests
REM   RunQuestPlanScan.bat -Root=Data\Quests -OutputJson=plan.json
REM   RunQuestPlanScan.bat -Root=Data\Quests -FailOn=differences
REM
REM -Root is required and is walked RECURSIVELY. Saved\ and Intermediate\ are
REM skipped. -FailOn defaults to "refusals" (fail only when a corpus CANNOT be
REM applied); use "differences" when your files are the source of truth and the
REM assets must always match them.
REM ============================================================================

setlocal

REM --- Configure UE install path here OR via the UE_PATH environment variable.
set "UE_PATH_INLINE=D:\Program Files\UE_5.6"

if not defined UE_PATH (
    if defined UE_PATH_INLINE (
        set "UE_PATH=%UE_PATH_INLINE%"
    ) else (
        echo ERROR: UE_PATH not set. Edit this script or export UE_PATH in your environment.
        exit /b 2
    )
)

set "UE_EXE=%UE_PATH%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
if not exist "%UE_EXE%" (
    echo ERROR: UnrealEditor-Cmd.exe not found at "%UE_EXE%"
    exit /b 2
)

REM Resolve project path: parent dir of Scripts\, looking for the .uproject.
set "PROJECT_DIR=%~dp0.."
for %%F in ("%PROJECT_DIR%\*.uproject") do set "PROJECT_FILE=%%~fF"
if not defined PROJECT_FILE (
    echo ERROR: No .uproject found in "%PROJECT_DIR%"
    exit /b 2
)

REM Caught here as well as in the commandlet purely to save a 60-second editor
REM boot on a typo. PRESENCE only — the commandlet still owns what a valid root
REM or a valid -FailOn mode is, so the contract lives in one place.
echo %* | findstr /I /C:"-Root=" >nul
if errorlevel 1 (
    echo ERROR: -Root=^<dir^> is required.
    echo   e.g. RunQuestPlanScan.bat -Root=Data\Quests
    exit /b 2
)

echo Running QuestPlanScan commandlet
echo   UE:      %UE_EXE%
echo   Project: %PROJECT_FILE%
echo   Args:    %*
echo.

"%UE_EXE%" "%PROJECT_FILE%" -run=QuestPlanScan -unattended -nopause -stdout %*
exit /b %ERRORLEVEL%

