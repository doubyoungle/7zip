# test-smart-extract.ps1
# Automated tests for the "Smart Extract" feature (7z x -sme switch).
# Usage: .github\workflows\test-smart-extract.ps1 -BinDir <dir with 7za.exe>
# Results are written to $env:GITHUB_STEP_SUMMARY (when available) and
# to .\test-output\. The script exits non-zero if any test fails.

param(
  [Parameter(Mandatory = $true)]
  [string]$BinDir
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$sevenza = Join-Path $BinDir '7za.exe'
if (-not (Test-Path $sevenza)) { throw "7za.exe not found: $sevenza" }

$root = Join-Path (Get-Location) 'test-output'
if (Test-Path $root) { Remove-Item $root -Recurse -Force }
New-Item -ItemType Directory -Force -Path $root | Out-Null

$results = New-Object System.Collections.Generic.List[string]
$failed = 0

function Write-CaseResult([string]$name, [bool]$ok, [string]$details) {
  $script:results.Add("| $name | $(if ($ok) { 'PASS' } else { 'FAIL' }) | $details |")
  if (-not $ok) { $script:failed++ }
}

# Creates a working directory for a case and returns its path
function New-CaseDir([string]$name) {
  $d = Join-Path $root $name
  New-Item -ItemType Directory -Force -Path $d | Out-Null
  return $d
}

# Asserts that a relative path exists (file or dir) inside the case dir
function Test-Layout([string]$caseDir, [string[]]$mustExist, [string[]]$mustNotExist) {
  foreach ($p in $mustExist) {
    if (-not (Test-Path (Join-Path $caseDir $p))) { return $false }
  }
  foreach ($p in $mustNotExist) {
    if (Test-Path (Join-Path $caseDir $p)) { return $false }
  }
  return $true
}

Push-Location $root
try {
  # ---------- build the test archives ----------
  $src = Join-Path $root 'src'
  New-Item -ItemType Directory -Force -Path "$src\foo", "$src\b" | Out-Null
  Set-Content -Path "$src\foo\a.txt" -Value 'aaa'
  Set-Content -Path "$src\b\c.txt" -Value 'ccc'
  Set-Content -Path "$src\a.txt" -Value 'top'
  $arc = Join-Path $root 'archives'
  New-Item -ItemType Directory -Force -Path $arc | Out-Null

  Push-Location $src
  try {
    & $sevenza a -tzip "$arc\single_folder.zip" foo | Out-Null
    & $sevenza a -tzip "$arc\single_file.zip" a.txt | Out-Null
    & $sevenza a -tzip "$arc\multi.zip" a.txt b | Out-Null
    & $sevenza a -ttar "$arc\foo.tar" foo | Out-Null
    & $sevenza a -tgzip "$arc\foo.tar.gz" "$arc\foo.tar" | Out-Null
    & $sevenza a -v10k "$arc\foo.7z" foo | Out-Null
    & $sevenza a -mhe=on -p123 "$arc\enc.7z" a.txt b | Out-Null
  }
  finally { Pop-Location }

  # a truly empty zip (only the End-Of-Central-Directory record)
  $emptyZip = Join-Path $arc 'empty.zip'
  $bytes = [byte[]](0x50, 0x4B, 0x05, 0x06) + [byte[]](@([byte]0) * 18)
  [IO.File]::WriteAllBytes($emptyZip, $bytes)

  if (-not (Test-Path "$arc\single_folder.zip")) { throw 'failed to create test archives' }

  # ---------- helper for running one case ----------
  function Run-SmartExtract([string]$name, [string]$archiveName, [string[]]$extraArgs,
                            [string[]]$mustExist, [string[]]$mustNotExist) {
    $d = New-CaseDir $name
    Copy-Item (Join-Path $arc $archiveName) $d
    Push-Location $d
    try {
      & $sevenza x -sme @extraArgs $archiveName -y *> extract.log
      $code = $LASTEXITCODE
      $ok = Test-Layout $d $mustExist $mustNotExist
      Write-CaseResult $name $ok "exit=$code; layout $(if ($ok) { 'ok' } else { 'wrong' })"
      if (-not $ok) { Get-Content (Join-Path $d 'extract.log') | Select-Object -First 20 | Write-Host }
    }
    finally { Pop-Location }
  }

  # ---------- test cases ----------
  # 1) archive with a single top-level folder: keep the folder, extract to current dir
  Run-SmartExtract '01_single_folder' 'single_folder.zip' @() `
      @('foo\a.txt') @('single_folder\foo\a.txt')

  # 2) archive with a single top-level file: extract to current dir
  Run-SmartExtract '02_single_file' 'single_file.zip' @() `
      @('a.txt') @('single_file\a.txt')

  # 3) archive with several top-level items: extract to subfolder named after archive
  Run-SmartExtract '03_multi' 'multi.zip' @() `
      @('multi\a.txt', 'multi\b\c.txt') @('a.txt', 'b\c.txt')

  # 4) compound archive foo.tar.gz: contents of the wrapped tar are used
  Run-SmartExtract '04_tar_gz' 'foo.tar.gz' @() `
      @('foo\a.txt') @('foo.tar')

  # 5) split volume foo.7z.001: base name foo is used
  $d = New-CaseDir '05_volume'
  Copy-Item (Join-Path $arc 'foo.7z.001') $d
  Push-Location $d
  try {
    & $sevenza x -sme foo.7z.001 -y *> extract.log
    $code = $LASTEXITCODE
    $ok = Test-Layout $d @('foo\a.txt') @('foo.7z.001\foo\a.txt')
    Write-CaseResult '05_volume' $ok "exit=$code; layout $(if ($ok) { 'ok' } else { 'wrong' })"
  }
  finally { Pop-Location }

  # 6) encrypted archive (headers encrypted): fallback to "extract to folder"
  Run-SmartExtract '06_encrypted' 'enc.7z' @('-p123') `
      @('enc\a.txt', 'enc\b\c.txt') @('a.txt', 'b\c.txt')

  # 7) empty archive: fallback mode, must not fail
  Run-SmartExtract '07_empty' 'empty.zip' @() @() @()

  # 8) regression: plain "x" without -sme keeps the standard behavior
  $d = New-CaseDir '08_plain_x_regression'
  Copy-Item (Join-Path $arc 'multi.zip') $d
  Push-Location $d
  try {
    & $sevenza x multi.zip -y *> extract.log
    $code = $LASTEXITCODE
    $ok = Test-Layout $d @('a.txt', 'b\c.txt') @('multi\a.txt')
    Write-CaseResult '08_plain_x_regression' $ok "exit=$code"
  }
  finally { Pop-Location }
}
finally {
  Pop-Location
}

# ---------- summary ----------
$summary = @()
$summary += '## Smart Extract test results'
$summary += ''
$summary += '| Case | Result | Details |'
$summary += '|------|--------|---------|'
$summary += $results
$summary += ''
$summary += "Total failed: $failed"
$text = $summary -join "`r`n"
$text | Out-Host
if ($env:GITHUB_STEP_SUMMARY) { $text | Add-Content $env:GITHUB_STEP_SUMMARY }

if ($failed -gt 0) { throw "$failed test case(s) failed" }
Write-Host 'All smart extract tests passed.'
