param([string]$Installer)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if (-not $Installer) { $Installer = Join-Path $repo 'dist\NetSwitch-2.1.0-Setup-x64.exe' }
$reportDir = Join-Path $repo 'build\smoke'
New-Item -ItemType Directory -Path $reportDir -Force | Out-Null
$installed = Join-Path $env:ProgramFiles '网卡切换器\NetSwitch.exe'
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
if (-not ([Security.Principal.WindowsPrincipal]::new($identity)).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Installer smoke test requires an elevated shell.' }
if (Get-Service -Name LocalNetSwitch -ErrorAction SilentlyContinue) { throw 'Use a clean test machine: an existing NetSwitch service must not be removed by this smoke test.' }
$success = $false
try {
    $setup = Start-Process -FilePath $Installer -ArgumentList @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART',('/LOG="'+(Join-Path $reportDir 'install.log')+'"')) -WindowStyle Hidden -PassThru -Wait
    if ($setup.ExitCode -ne 0) { throw ('Installer exit code: '+$setup.ExitCode) }
    if (-not (Test-Path -LiteralPath $installed)) { throw 'Installed executable was not found.' }
    if ((Get-Service LocalNetSwitch).Status -ne 'Running') { throw 'Service is not running.' }
    $healthFile = Join-Path $reportDir 'health.json'
    $health = Start-Process -FilePath $installed -ArgumentList @('--health',('"'+$healthFile+'"')) -WindowStyle Hidden -PassThru -Wait
    if ($health.ExitCode -ne 0 -or -not (Get-Content -LiteralPath $healthFile -Raw | ConvertFrom-Json).Success) { throw 'Service health check failed.' }
    if ((Get-ItemProperty -LiteralPath 'HKLM:\Software\LocalNetSwitch').AccessScope -ne 'LocalUsers') { throw 'Installed access scope is incorrect.' }
    $success = $true
} finally {
    $uninstaller = Join-Path (Split-Path -Parent $installed) 'unins000.exe'
    if (Test-Path -LiteralPath $uninstaller) {
        $uninstall = Start-Process -FilePath $uninstaller -ArgumentList @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART',('/LOG="'+(Join-Path $reportDir 'uninstall.log')+'"')) -WindowStyle Hidden -PassThru -Wait
        if ($uninstall.ExitCode -ne 0) { throw ('Uninstaller exit code: '+$uninstall.ExitCode) }
    }
}
if (Get-Service -Name LocalNetSwitch -ErrorAction SilentlyContinue) { throw 'Service remained after uninstall.' }
if (Test-Path -LiteralPath $installed) { throw 'Executable remained after uninstall.' }
if (-not $success) { throw 'Installer smoke test failed.' }
Write-Output 'Installation, service health, local-user access configuration, and uninstall passed. No adapters were switched.'
