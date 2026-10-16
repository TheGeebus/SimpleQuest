<#
.SYNOPSIS
    Confirms a headless test run ran exactly the tests the source declares.

.DESCRIPTION
    The engine's exit code says whether the tests that RAN passed. It cannot say whether the right tests ran: a test
    whose module did not load, or that was compiled out, is simply absent, and the run still exits 0.

    This reads the report RunAutomationTests.bat writes and fails unless the number of tests that ran EQUALS the
    expected count. By default the expected count is worked out from the source: every IMPLEMENT_SIMPLE_AUTOMATION_TEST
    under Plugins\*\Source, so adding a test needs no other change. When the counts disagree, it names the tests that
    are declared but did not run, and the ones that ran but are not declared.

    What a count from the source cannot catch: a test file left out of a commit, or a test deleted by mistake. Either
    one lowers the declared count and the run's count together.

    A test form whose number of tests the source does not show - IMPLEMENT_COMPLEX_AUTOMATION_TEST, which runs once per
    parameter, or an automation spec - stops the check rather than being guessed at. Pass -Expected to check such a
    run by hand.

    Exit codes: 0 the count matches and nothing failed, 1 the count is wrong or a test failed, 2 the check could not
    run (no readable report, or no expected count could be worked out).

.EXAMPLE
    powershell -NoProfile -ExecutionPolicy Bypass -File Scripts\CheckTestCount.ps1

.EXAMPLE
    powershell -NoProfile -ExecutionPolicy Bypass -File Scripts\CheckTestCount.ps1 -Expected 134
#>

param(
# Overrides the count worked out from the source. Leave it out unless the source cannot be counted.
    [int] $Expected = -1,

# Only needed to check a report somewhere other than where RunAutomationTests.bat writes it.
    [string] $ReportDir = '',

# Only needed to count the tests of a checkout other than the one this script is in.
    [string] $ProjectDir = ''
)

if (-not $ProjectDir)
{
    $ProjectDir = Join-Path $PSScriptRoot '..'
}
$ProjectDir = [IO.Path]::GetFullPath($ProjectDir).TrimEnd('\')
if (-not $ReportDir)
{
    $ReportDir = Join-Path $ProjectDir 'Saved\Automation\Headless'
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

# --- The expected count --------------------------------------------------------------------------------------------

# Anchored at the start of a line, so a declaration mentioned in a // or * comment is not counted. The test's name is
# the string after the class name; the macro may wrap its arguments onto the next line, which \s covers.
$SimplePattern = '(?m)^[ \t]*IMPLEMENT_(?:CUSTOM_)?SIMPLE_AUTOMATION_TEST\s*\(\s*\w+\s*,\s*(?:\w+\s*,\s*)?"([^"]+)"'
$SimpleDeclaration = '(?m)^[ \t]*IMPLEMENT_(?:CUSTOM_)?SIMPLE_AUTOMATION_TEST\s*\('
$Uncountable = '(?m)^[ \t]*(IMPLEMENT_(?:CUSTOM_)?COMPLEX_AUTOMATION_TEST|BEGIN_(?:CUSTOM_)?COMPLEX_AUTOMATION_TEST|' +
        'BEGIN_DEFINE_SPEC|DEFINE_SPEC|IMPLEMENT_NETWORKED_AUTOMATION_TEST)\b'

$DeclaredNames = @()
if ($Expected -ge 0)
{
    $Source = 'given with -Expected'
}
else
{
    $SourceRoots = @(Get-ChildItem -Path (Join-Path $ProjectDir 'Plugins') -Directory -ErrorAction SilentlyContinue |
            ForEach-Object { Join-Path $_.FullName 'Source' } | Where-Object { Test-Path -LiteralPath $_ })
    $Files = @($SourceRoots | ForEach-Object { Get-ChildItem -LiteralPath $_ -Recurse -File } |
            Where-Object { $_.Extension -eq '.cpp' -or $_.Extension -eq '.h' })

    $Declared = 0
    $DeclaringFiles = 0
    $Blockers = @()
    foreach ($File in $Files)
    {
        $Text = Get-Content -LiteralPath $File.FullName -Raw -Encoding UTF8
        if (-not $Text) { continue }

        foreach ($Match in [regex]::Matches($Text, $Uncountable))
        {
            $Line = ($Text.Substring(0, $Match.Index) -split "`n").Count
            $Blockers += '{0}:{1} {2}' -f $File.FullName.Substring($ProjectDir.Length + 1), $Line, $Match.Groups[1].Value
        }

        $Count = [regex]::Matches($Text, $SimpleDeclaration).Count
        if ($Count -gt 0)
        {
            $Declared += $Count
            $DeclaringFiles++
            $DeclaredNames += @([regex]::Matches($Text, $SimplePattern) | ForEach-Object { $_.Groups[1].Value })
        }
    }

    if ($Blockers.Count -gt 0)
    {
        Write-Host "CheckTestCount: FAILED - the source declares tests whose number it does not show, so no count can be worked out:"
        foreach ($Blocker in $Blockers) { Write-Host "  $Blocker" }
        Write-Host "  Teach this script to count them, or pass -Expected with the right total."
        exit 2
    }
    if ($Declared -eq 0)
    {
        Write-Host "CheckTestCount: FAILED - found no IMPLEMENT_SIMPLE_AUTOMATION_TEST under '$ProjectDir\Plugins\*\Source'."
        Write-Host "  The project folder is wrong, or the tests moved. Pass -ProjectDir, or -Expected."
        exit 2
    }

    $Expected = $Declared
    $Source = "counted from the source: $Declared declared in $DeclaringFiles file(s)"
}

# --- The check -----------------------------------------------------------------------------------------------------

$Ran = [int]$Report.succeeded + [int]$Report.succeededWithWarnings + [int]$Report.failed
$Summary = '{0} ran: {1} passed, {2} passed with warnings, {3} failed, {4} not run. Expected {5} ({6}).'
Write-Host ('CheckTestCount: ' + ($Summary -f $Ran, $Report.succeeded, $Report.succeededWithWarnings, $Report.failed, $Report.notRun, $Expected, $Source))

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

if ($Ran -ne $Expected)
{
    if ($Ran -lt $Expected)
    {
        Write-Host "CheckTestCount: FAILED - $($Expected - $Ran) fewer test(s) ran than expected."
        Write-Host "  Some went missing: a module that did not load, or a test compiled out on this engine."
    }
    else
    {
        Write-Host "CheckTestCount: FAILED - $($Ran - $Expected) more test(s) ran than expected."
        Write-Host "  Tests ran that the source does not declare: another plugin's tests matched the filter, or a test form this"
        Write-Host "  script does not recognize."
    }

    # Names only when they came from the source. Against a hand-given count there is nothing to compare them with.
    if ($DeclaredNames.Count -gt 0)
    {
        $RanNames = @($Report.tests | Where-Object { $_.state -eq 'Success' -or $_.state -eq 'Fail' } | ForEach-Object { $_.fullTestPath })
        $NotRun = @($DeclaredNames | Where-Object { $RanNames -notcontains $_ } | Sort-Object)
        $NotDeclared = @($RanNames | Where-Object { $DeclaredNames -notcontains $_ } | Sort-Object)
        if ($NotRun.Count -gt 0)
        {
            Write-Host "  Declared in the source but did not run:"
            foreach ($Name in $NotRun) { Write-Host "    $Name" }
        }
        if ($NotDeclared.Count -gt 0)
        {
            Write-Host "  Ran but not declared in the source:"
            foreach ($Name in $NotDeclared) { Write-Host "    $Name" }
        }
    }
    $bFailed = $true
}

if ($bFailed)
{
    exit 1
}

Write-Host "CheckTestCount: OK - all $Ran expected tests ran and passed."
exit 0
