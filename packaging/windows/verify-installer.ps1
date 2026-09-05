param(
    [Parameter(Mandatory = $true)]
    [string]$InstallerPath
)

$ErrorActionPreference = "Stop"

$InstallerPath = Resolve-Path $InstallerPath
$InstallDir = Join-Path $env:TEMP "speecher-installer-test-$PID"
$OriginalPath = $env:Path
$OriginalPlatform = $env:QT_QPA_PLATFORM
$OriginalGrabPage = $env:SPEECHER_GRAB_PAGE
$App = $null

try {
    $Arguments = @(
        "/VERYSILENT",
        "/SUPPRESSMSGBOXES",
        "/NORESTART",
        "/VERIFYINSTALL=1",
        "/DIR=`"$InstallDir`""
    )
    $Installer = Start-Process $InstallerPath -ArgumentList $Arguments -Wait -PassThru
    if ($Installer.ExitCode -ne 0) {
        throw "Installer exited with code $($Installer.ExitCode)"
    }

    $Exe = Join-Path $InstallDir "speecher.exe"
    foreach ($Required in "Qt6WebSockets.dll", "Qt6Multimedia.dll", "platforms\qoffscreen.dll") {
        if (-not (Test-Path (Join-Path $InstallDir $Required))) {
            throw "Installed application is missing $Required"
        }
    }
    if (-not (Get-ChildItem (Join-Path $InstallDir "multimedia") -Filter *.dll)) {
        throw "Installed application has no multimedia plugins"
    }

    $env:Path = "$env:SystemRoot;$env:SystemRoot\System32;$env:SystemRoot\System32\Wbem;$env:SystemRoot\System32\WindowsPowerShell\v1.0"
    $env:QT_QPA_PLATFORM = "offscreen"
    $env:SPEECHER_GRAB_PAGE = "general"
    $Grab = Join-Path $InstallDir "installer-readiness.png"
    $App = Start-Process $Exe -ArgumentList "--grab", "`"$Grab`"" -PassThru
    if (-not $App.WaitForExit(45000)) {
        $App | Stop-Process -Force
        throw "Installed application did not become ready within 45 seconds"
    }
    if ($App.ExitCode -ne 0 -or -not (Test-Path $Grab)) {
        throw "Installed application readiness check failed with exit code $($App.ExitCode)"
    }
    Write-Output "Installed application produced $Grab without Qt on PATH"
} finally {
    if ($App -and -not $App.HasExited) {
        $App | Stop-Process -Force
    }
    $env:Path = $OriginalPath
    $env:QT_QPA_PLATFORM = $OriginalPlatform
    $env:SPEECHER_GRAB_PAGE = $OriginalGrabPage
    $Uninstaller = Join-Path $InstallDir "unins000.exe"
    if (Test-Path $Uninstaller) {
        Start-Process $Uninstaller -ArgumentList "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART" -Wait
    }
    if (Test-Path $InstallDir) {
        Remove-Item $InstallDir -Recurse -Force
    }
}
