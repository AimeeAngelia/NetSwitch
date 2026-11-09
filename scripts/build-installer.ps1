param([string]$IsccPath)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if (-not (Test-Path -LiteralPath (Join-Path $repo 'build\NetSwitch.exe'))) { throw 'Run build.cmd first.' }
if (-not $IsccPath) {
    $command = Get-Command ISCC.exe -ErrorAction SilentlyContinue
    if ($command) { $IsccPath = $command.Source }
    else {
        foreach ($candidate in @("$env:ProgramFiles\Inno Setup 7\ISCC.exe", "${env:ProgramFiles(x86)}\Inno Setup 7\ISCC.exe")) {
            if (Test-Path -LiteralPath $candidate) { $IsccPath=$candidate; break }
        }
    }
}
if (-not $IsccPath -or -not (Test-Path -LiteralPath $IsccPath)) { throw 'Inno Setup 7.1.1 is required; pass -IsccPath.' }
& $IsccPath (Join-Path $repo 'installer\NetSwitch.iss')
if ($LASTEXITCODE -ne 0) { throw 'Installer compilation failed.' }
