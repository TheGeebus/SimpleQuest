@echo off
REM ============================================================================
REM Stale Quest Tags Scan — Windows helper script
REM
REM Runs the SimpleQuest stale-tag commandlet headlessly. Intended for pre-tag-
REM consolidation validation, pre-release sweeps, or CI gating.
REM
REM Set UE_PATH below to your local Unreal install root, OR set the UE_PATH
REM environment variable in your shell. The script falls back to the env var if
REM the line below is left empty.
REM
REM Exit codes:
REM   0   no stale references found
REM   1   one or more stale references found
REM   2   commandlet failed (could not init, JSON write failed, etc.)
REM
REM Usage:
REM   RunStaleQuestTagsScan.bat                          (log output only)
REM   RunStaleQuestTagsScan.bat -OutputJson=results.json (also write JSON)
REM   RunStaleQuestTagsScan.bat -FastWP                  (class-filter WP scan)
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

echo Running StaleQuestTagsScan commandlet
echo   UE:      %UE_EXE%
echo   Project: %PROJECT_FILE%
echo   Args:    %*
echo.

"%UE_EXE%" "%PROJECT_FILE%" -run=StaleQuestTagsScan -unattended -nopause -stdout %*
exit /b %ERRORLEVEL%