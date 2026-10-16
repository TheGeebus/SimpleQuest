<#
.SYNOPSIS
    Confirms a headless test run ran exactly the number of tests it was supposed to.

.DESCRIPTION
    The engine's exit code says whether the tests that RAN passed. It cannot say whether the right tests ran: a test
    whose module did not load, whose file was left out of a commit, or that was compiled out is simply absent, and
    the run still exits 0.

    This reads the report RunAutomationTests.bat writes and fails unless the number of tests that ran EQUALS the
    expected count. Exactly, not "at least": adding a test without raising the count fails too, so the count is raised
    in the same commit as the test and never drifts below the truth.

    Exit codes: 0 the count matches and nothing failed, 1 the count is wrong or a test failed, 2 no readable report.

.EXAMPLE
    powershell -NoProfile -ExecutionPolicy Bypass -File Scripts\CheckTestCount.ps1 -Expected 134
#>

param(
    [Parameter(Mandatory = $true)]
    [int] $Expected,

    # Only needed to check a report somewhere other than where RunAutomationTests.bat writes it.
    [string] $ReportDir = ''
)

if (-not $ReportDir)
{
    $ReportDir = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\Saved\Automation\Headless'))
}

$ReportFile = Join-Path $ReportDir 'index.json'
if (-not (Test-Path -LiteralPath $ReportFile))
{
    Write-Host "CheckTestCount: FAILED - no report at '$ReportFile'."
    Write-Host "  The run never reached the end of its tests, or wrote its report somewhere else. Read the editor log."
    exit 2
}

try
{
    $Report = Get-Content -LiteralPath $ReportFile -Raw -Encoding UTF8 | ConvertFrom-Json
}
catch
{
    Write-Host "CheckTestCount: FAILED - '$ReportFile' is not readable JSON: $($_.Exception.Message)"
    exit 2
}

# A missing field reads as nothing, and nothing adds up as 0, so a renamed field would show up as "0 tests ran" - a
# true failure with a misleading reason. Name the real one instead.
foreach ($Field in 'succeeded', 'succeededWithWarnings', 'failed', 'notRun')
{
    if ($null -eq $Report.$Field)
    {
        Write-Host "CheckTestCount: FAILED - the report has no '$Field' field, so the engine's report format may have changed."
        Write-Host "  Open '$ReportFile' and update the field names in this script."
        exit 2
    }
}

$Ran = [int]$Report.succeeded + [int]$Report.succeededWithWarnings + [int]$Report.failed
$Summary = '{0} ran: {1} passed, {2} passed with warnings, {3} failed, {4} not run. Expected {5}.'
Write-Host ('CheckTestCount: ' + ($Summary -f $Ran, $Report.succeeded, $Report.succeededWithWarnings, $Report.failed, $Report.notRun, $Expected))

$bFailed = $false

if ([int]$Report.failed -gt 0)
{
    # The engine's own exit code already fails the run. Naming the tests here puts them at the bottom of the log, which
    # is the part of a red run that gets read first.
    Write-Host "CheckTestCount: FAILED - $($Report.failed) test(s) failed:"
    $FailedTests = @($Report.tests | Where-Object { $_.state -eq 'Fail' })
    foreach ($Test in $FailedTests)
    {
        Write-Host "  $($Test.fullTestPath)"
    }
    if ($FailedTests.Count -eq 0)
    {
        Write-Host "  (no names found in the report's test list - open index.html in '$ReportDir')"
    }
    $bFailed = $true
}

if ($Ran -lt $Expected)
{
    Write-Host "CheckTestCount: FAILED - $($Expected - $Ran) fewer test(s) ran than expected."
    Write-Host "  Some went missing: a module that did not load, a file left out of the commit, or a test compiled out."
    Write-Host "  Compare the test list at the top of the log with the source."
    $bFailed = $true
}
elseif ($Ran -gt $Expected)
{
    Write-Host "CheckTestCount: FAILED - $($Ran - $Expected) more test(s) ran than expected."
    Write-Host "  If tests were added, raise the expected count to $Ran in the same commit."
    $bFailed = $true
}

if ($bFailed)
{
    exit 1
}

Write-Host "CheckTestCount: OK - all $Ran expected tests ran and passed."
exit 0
