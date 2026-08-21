# run-tagtoolbox-tag-audit.ps1 - the Tag Toolbox CI gate wrapper (U10).
#
# Contract (report-is-the-verdict; process exit is NOT authority):
#   exit 0  = fresh well-formed report, no findings at failure severity
#   exit 1  = fresh well-formed report, findings at failure severity
#   exit 2  = infrastructure failure (report missing, stale, malformed, or
#             wrong schema) - fails CLOSED
# Unreal derives its own process exit from any error logged during the run,
# so unrelated host-content errors would turn a clean audit red; the wrapper
# therefore takes its verdict strictly from a report this invocation
# provably produced. A nonzero process exit beside a clean report is
# surfaced as a loud warning, never an audit verdict.
#
# Default failure severity: referenced-but-undefined only (the shipped-bug
# class). Escalation switches add categories to the failure set; the report
# always carries ALL findings regardless.

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ProjectPath,

    [string]$EditorCmd,
    [string]$EnginePath,

    [string]$JsonOutput,

    [switch]$FailOnUnused,
    [switch]$FailOnBrokenRedirects,
    [switch]$FailOnLingeringRedirects,
    [switch]$FailOnNearDuplicates,

    # Referencer scope: game content only by default.
    [switch]$IncludeEngineContent,
    [switch]$IncludePluginContent,

    [int]$TimeoutMinutes = 30
)

$ErrorActionPreference = 'Stop'

function Fail-Infra([string]$Message) {
    Write-Host "[tagtoolbox-audit] INFRASTRUCTURE FAILURE: $Message" -ForegroundColor Red
    exit 2
}

if (-not (Test-Path $ProjectPath)) { Fail-Infra "Project file not found: $ProjectPath" }
$ProjectPath = (Resolve-Path $ProjectPath).Path

if (-not $EditorCmd) {
    if (-not $EnginePath) { Fail-Infra "Pass -EditorCmd (path to UnrealEditor-Cmd.exe) or -EnginePath (engine root)." }
    $EditorCmd = Join-Path $EnginePath 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
}
if (-not (Test-Path $EditorCmd)) { Fail-Infra "UnrealEditor-Cmd.exe not found: $EditorCmd" }

if (-not $JsonOutput) {
    $JsonOutput = Join-Path (Split-Path $ProjectPath -Parent) 'Saved\TagToolbox\TagAuditReport.json'
}
$ReportDir = Split-Path $JsonOutput -Parent
if (-not (Test-Path $ReportDir)) { New-Item -ItemType Directory -Force $ReportDir | Out-Null }

# Fail-closed freshness: delete any stale report and record the start time.
if (Test-Path $JsonOutput) { Remove-Item $JsonOutput -Force -Confirm:$false }
$StartTime = Get-Date

$CommandletArgs = @(
    "`"$ProjectPath`"",
    '-run=TagToolboxTagAudit',
    "-ReportPath=`"$JsonOutput`"",
    '-unattended', '-nop4', '-NoSplash', '-nullrhi'
)
if ($FailOnUnused)             { $CommandletArgs += '-FailOnUnused' }
if ($FailOnBrokenRedirects)    { $CommandletArgs += '-FailOnBrokenRedirects' }
if ($FailOnLingeringRedirects) { $CommandletArgs += '-FailOnLingeringRedirects' }
if ($FailOnNearDuplicates)     { $CommandletArgs += '-FailOnNearDuplicates' }
if ($IncludeEngineContent)     { $CommandletArgs += '-IncludeEngineContent' }
if ($IncludePluginContent)     { $CommandletArgs += '-IncludePluginContent' }

Write-Host "[tagtoolbox-audit] Running: $EditorCmd $($CommandletArgs -join ' ')"
$Process = Start-Process -FilePath $EditorCmd -ArgumentList $CommandletArgs -NoNewWindow -PassThru
if (-not $Process.WaitForExit($TimeoutMinutes * 60 * 1000)) {
    try { $Process.Kill($true) } catch {}
    Fail-Infra "Commandlet did not finish within $TimeoutMinutes minutes."
}
# The timed WaitForExit overload does not populate ExitCode; the void overload
# (returning immediately here) flushes it.
$Process.WaitForExit()
$ProcessExit = $Process.ExitCode

# --- Verdict strictly from a report THIS invocation produced.
if (-not (Test-Path $JsonOutput)) { Fail-Infra "Report was not produced at $JsonOutput (process exit $ProcessExit)." }
$ReportItem = Get-Item $JsonOutput
if ($ReportItem.LastWriteTime -lt $StartTime) { Fail-Infra "Report at $JsonOutput predates this run - refusing a stale verdict." }

try {
    $Report = Get-Content $JsonOutput -Raw | ConvertFrom-Json
} catch {
    Fail-Infra "Report is not valid JSON: $($_.Exception.Message)"
}
if ($null -eq $Report.schema_version) { Fail-Infra "Report carries no schema_version." }
if ([int]$Report.schema_version -ne 1) { Fail-Infra "Unsupported report schema_version '$($Report.schema_version)' (expected 1)." }
if ($null -eq $Report.failed -or $null -eq $Report.counts) { Fail-Infra "Report is missing required fields (failed/counts)." }

$Counts = $Report.counts
Write-Host ("[tagtoolbox-audit] Findings: {0} undefined, {1} unused, {2} near-duplicate, {3} broken redirect(s), {4} lingering redirect(s)" -f `
    $Counts.referenced_undefined, $Counts.unused, $Counts.near_duplicate, $Counts.broken_redirect, $Counts.lingering_redirect)

if ($Report.failed) {
    Write-Host "[tagtoolbox-audit] FAIL: findings at failure severity. Failing tags:" -ForegroundColor Red
    foreach ($Finding in $Report.findings) {
        if ($Finding.fails) {
            Write-Host ("  [{0}] {1} - {2}" -f $Finding.category, $Finding.tag, $Finding.detail) -ForegroundColor Red
        }
    }
    exit 1
}

if ($ProcessExit -ne 0) {
    Write-Host "[tagtoolbox-audit] WARNING: the editor process exited $ProcessExit while the audit report is clean. Unreal derives its exit code from ANY error logged during the run, so this is almost always unrelated host-content noise - but check the log if it is new." -ForegroundColor Yellow
}

Write-Host "[tagtoolbox-audit] PASS: no findings at failure severity." -ForegroundColor Green
exit 0
