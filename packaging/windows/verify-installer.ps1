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

    # Open with: offered for audio files without becoming their default.
    $Command = (Get-ItemProperty "HKCU:\Software\Classes\Speecher.AudioFile\shell\open\command")."(default)"
    if ($Command -ne "`"$Exe`" `"%1`"") {
        throw "Open with command is '$Command'"
    }
    foreach ($Extension in ".wav", ".mp3", ".m4a", ".flac", ".ogg") {
        $Progids = Get-Item "HKCU:\Software\Classes\$Extension\OpenWithProgids" -ErrorAction SilentlyContinue
        if (-not $Progids -or $Progids.GetValueNames() -notcontains "Speecher.AudioFile") {
            throw "$Extension does not offer Speecher under Open with"
        }
        if ((Get-ItemProperty "HKCU:\Software\Classes\$Extension" -ErrorAction SilentlyContinue)."(default)" -eq "Speecher.AudioFile") {
            throw "The installer made Speecher the default for $Extension"
        }
    }

    # Launch with only system directories on PATH to prove the install is
    # self-contained. WinUI 3 cannot render into the offscreen QPA platform
    # (it needs a real HWND), so this opens the settings window on the runner's
    # interactive desktop and checks the process comes up and stays alive
    # rather than grabbing an offscreen frame.
    $env:Path = "$env:SystemRoot;$env:SystemRoot\System32;$env:SystemRoot\System32\Wbem;$env:SystemRoot\System32\WindowsPowerShell\v1.0"
    $env:QT_QPA_PLATFORM = $null
    $App = Start-Process $Exe -ArgumentList "--show-settings" -PassThru
    Start-Sleep -Seconds 8
    $App.Refresh()
    # A missing runtime dependency (Qt, the WinUI bootstrap, a plugin) makes the
    # process fault during startup, so surviving several seconds is the
    # self-containment signal. WinUI windows do not set the Win32
    # MainWindowHandle, so that is not a reliable readiness check here.
    if ($App.HasExited) {
        throw "Installed application exited during startup with code $($App.ExitCode) (missing runtime dependency?)"
    }
    Write-Output "Installed application launched and stayed alive without Qt on PATH"
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
    if (Test-Path "HKCU:\Software\Classes\Speecher.AudioFile") {
        throw "Uninstall left the Speecher.AudioFile ProgId behind"
    }
    if (Test-Path $InstallDir) {
        Remove-Item $InstallDir -Recurse -Force
    }
}
