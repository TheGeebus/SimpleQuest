@echo off
REM ============================================================================
REM Automation Tests — Windows helper script
REM
REM Runs every SimpleQuest and SimpleCore automation test headlessly: no editor
REM window and no rendering. Intended for pre-release sweeps and CI gating, and
REM just as useful by hand after a change.
REM
REM Set UE_PATH below to your local Unreal install root, OR set the UE_PATH
REM environment variable in your shell. The environment variable wins when both
REM are set.
REM
REM The report (index.json and index.html) always goes to
REM Saved\Automation\Headless under the project. That folder is deleted at the
REM start of every run, so a report found there always belongs to the last run.
REM Scripts\CheckTestCount.ps1 reads it.
REM
REM Exit codes (the same contract as the other wrappers in Scripts\):
REM   0   every test passed
REM   1   the tests ran and at least one failed
REM   2   the run could not finish: this script could not find the engine or
REM       the project, the editor crashed or failed to start, no test matched
REM       the filter, or the test runner found no worker to run on
REM
REM Usage:
REM   RunAutomationTests.bat
REM   RunAutomationTests.bat <extra editor arguments, passed through unchanged>
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

REM The engine resolves a relative report path against its own Engine\Binaries\Win64 folder, not the project's, so the
REM report folder is built as a full path from the project file's location.
for %%F in ("%PROJECT_FILE%") do set "REPORT_DIR=%%~dpFSaved\Automation\Headless"

REM Deleted first so a run that dies before writing its report leaves NO report, rather than the last run's passing one.
if exist "%REPORT_DIR%" rmdir /s /q "%REPORT_DIR%"

REM Prefix matches on the test name, joined with "+". "StartsWith:SimpleQuest" matches names beginning "SimpleQuest.",
REM so a test elsewhere that merely mentions SimpleQuest in its name is never swept in.
set "TEST_FILTER=StartsWith:SimpleQuest+StartsWith:SimpleCore"

echo Running automation tests
echo   UE:      %UE_EXE%
echo   Project: %PROJECT_FILE%
echo   Filter:  %TEST_FILTER%
echo   Report:  %REPORT_DIR%
echo   Args:    %*
echo.

"%UE_EXE%" "%PROJECT_FILE%" -ExecCmds="Automation RunTests %TEST_FILTER%;Quit" -ReportExportPath="%REPORT_DIR%" -unattended -nopause -nosplash -NullRHI -stdout %*
set "UE_EXIT=%ERRORLEVEL%"

REM The editor's own codes, turned into this script's. The engine logs "EXIT CODE: -1" when a test fails, when no test
REM matches the filter, and when it finds no automation worker, but its exit call takes a single byte, so the process
REM really exits 255 for all three. Only a run whose tests actually ran writes a report, and the report folder was
REM emptied above, so 255 WITH a report means a test failed. A crash exits 3. Anything else means the editor stopped first.
if "%UE_EXIT%"=="0" exit /b 0
if "%UE_EXIT%"=="255" if exist "%REPORT_DIR%\index.json" (
    echo Tests failed: the editor exited 255, which its log prints as "EXIT CODE: -1".
    exit /b 1
)
if "%UE_EXIT%"=="255" (
    echo ERROR: the run failed before any test ran: no test matched the filter, or no automation worker was found.
    echo   The editor exited 255 and wrote no report. Search the log for "No automation tests matched" or "Failed to find workers".
    exit /b 2
)
echo ERROR: the editor exited with code %UE_EXIT% before the tests finished. Read the newest log in Saved\Logs.
exit /b 2

