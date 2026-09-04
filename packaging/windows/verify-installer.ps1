param(
    [Parameter(Mandatory = $true)]
    [string]$InstallerPath
)

$ErrorActionPreference = "Stop"

$InstallerPath = Resolve-Path $InstallerPath
$InstallDir = Join-Path $env:TEMP "speecher-installer-test-$PID"

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

    $VersionOutput = (& (Join-Path $InstallDir "speecher.exe") --version | Select-Object -First 1)
    if ($VersionOutput -notmatch '^speecher .+ \(build [0-9]+\)$') {
        throw "Installed executable returned an invalid version: $VersionOutput"
    }
    Write-Output $VersionOutput
} finally {
    $Uninstaller = Join-Path $InstallDir "unins000.exe"
    if (Test-Path $Uninstaller) {
        Start-Process $Uninstaller -ArgumentList "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART" -Wait
    }
    if (Test-Path $InstallDir) {
        Remove-Item $InstallDir -Recurse -Force
    }
}
