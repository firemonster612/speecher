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

# Starts the installed app and fails unless it is still running after startup.
function Start-Speecher([string]$Argument) {
    $Process = Start-Process $Exe -ArgumentList $Argument -PassThru
    Start-Sleep -Seconds 8
    $Process.Refresh()
    if ($Process.HasExited) {
        throw "Application started with $Argument exited during startup with code $($Process.ExitCode)"
    }
    return $Process
}

# Quits the background app through the launcher, which waits for the quit
# command and hands back its status. A native command's status is also the
# step's exit status if nothing runs after it, so it is checked here.
function Stop-BackgroundApp {
    & (Join-Path $InstallDir "speecher.com") quit | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "speecher.com quit exited with $LASTEXITCODE"
    }
}

# How many entries of the user's Path name the install folder.
function Get-PathEntryCount {
    @([Environment]::GetEnvironmentVariable("Path", "User") -split ";" | Where-Object { $_ -eq $InstallDir }).Count
}

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
    foreach ($Required in "speecher.com", "Qt6WebSockets.dll", "Qt6Multimedia.dll", "platforms\qoffscreen.dll", "transcribe.dll", "ggml-cpu-x64.dll", "ggml-vulkan.dll", "multimedia\ffmpegmediaplugin.dll", "networkinformation\qnetworklistmanager.dll") {
        if (-not (Test-Path (Join-Path $InstallDir $Required))) {
            throw "Installed application is missing $Required"
        }
    }
    if (-not (Get-ChildItem (Join-Path $InstallDir "multimedia") -Filter *.dll)) {
        throw "Installed application has no multimedia plugins"
    }

    # Every DLL an installed binary imports must ship beside speecher.exe or come
    # with Windows. The runner's System32 has the Visual C++ runtime and a clean
    # Windows install does not, so any DLL from the MSVC redist folder counts only
    # when it is app-local. API sets are resolved by the loader, not found as
    # files. Delay-load imports are left out: the WinUI ones come from the
    # Windows App Runtime package.
    if (-not $env:VCToolsRedistDir) {
        throw "VCToolsRedistDir is not set; run this from an MSVC developer shell"
    }
    $VcRuntime = (Get-ChildItem (Join-Path $env:VCToolsRedistDir "x64\*\*.dll")).Name
    if (-not $VcRuntime) {
        throw "No Visual C++ runtime DLLs found under VCToolsRedistDir '$env:VCToolsRedistDir'"
    }
    $Missing = foreach ($Binary in Get-ChildItem $InstallDir -Recurse -Include *.exe, *.com, *.dll) {
        $Dump = (& dumpbin /nologo /dependents $Binary.FullName) -join "`n"
        if ($LASTEXITCODE -ne 0) {
            throw "dumpbin failed on $($Binary.FullName) with exit code $LASTEXITCODE"
        }
        $Imports = [regex]::Match($Dump, '(?i)has the following dependencies:\s+((?:\S+\.\w+\s+)+)').Groups[1].Value
        if (-not $Imports) {
            throw "dumpbin listed no imports for $($Binary.FullName)"
        }
        foreach ($Dll in -split $Imports) {
            # vulkan-1.dll comes with the GPU driver and is optional: without it
            # ggml falls back to the CPU.
            $Found = (Test-Path (Join-Path $InstallDir $Dll)) -or $Dll -match '^(api|ext)-ms-' -or
                $Dll -eq "vulkan-1.dll" -or
                ($VcRuntime -notcontains $Dll -and (Test-Path (Join-Path "$env:SystemRoot\System32" $Dll)))
            if (-not $Found) {
                "$($Binary.FullName.Substring($InstallDir.Length + 1)) imports $Dll"
            }
        }
    }
    if ($Missing) {
        throw "Installed binaries import DLLs that neither ship with Speecher nor come with Windows:`n$($Missing -join "`n")"
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

    # The console launcher waits for speecher.exe and hands back its output
    # and exit status; a usage error exits with 2.
    $Launcher = Join-Path $InstallDir "speecher.com"
    $Status = & $Launcher status
    if ($LASTEXITCODE -ne 0 -or $Status -ne "idle") {
        throw "speecher.com status printed '$Status' and exited with $LASTEXITCODE"
    }
    & $Launcher status --format html 2>$null
    if ($LASTEXITCODE -ne 2) {
        throw "speecher.com returned $LASTEXITCODE for a usage error rather than 2"
    }
    if ((Get-PathEntryCount) -ne 1) {
        throw "The installer did not put $InstallDir on the user's Path"
    }
    Write-Output "speecher.com returned speecher.exe's output and exit status"

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
    $App | Stop-Process -Force
    $App.WaitForExit()

    # A run that goes on in the background releases the launcher, so the
    # prompt comes back while Speecher keeps running.
    $LauncherRun = Start-Process $Launcher -ArgumentList "--daemon" -PassThru
    $null = $LauncherRun.Handle # keeps ExitCode readable after it exits
    if (-not $LauncherRun.WaitForExit(10000)) {
        $LauncherRun | Stop-Process -Force
        throw "speecher.com --daemon was still waiting after 10 seconds"
    }
    if ($LauncherRun.ExitCode -ne 0) {
        throw "speecher.com --daemon exited with $($LauncherRun.ExitCode)"
    }
    # The launcher's console host is its child too, so match by name.
    $Child = Get-CimInstance Win32_Process -Filter "ParentProcessId = $($LauncherRun.Id) AND Name = 'speecher.exe'"
    $App = if ($Child) { Get-Process -Id $Child.ProcessId -ErrorAction SilentlyContinue }
    if (-not $App) {
        throw "speecher.com --daemon returned but speecher.exe is no longer running"
    }
    Stop-BackgroundApp
    if (-not $App.WaitForExit(10000)) {
        throw "speecher.exe quit did not stop the background app the launcher started"
    }
    Write-Output "speecher.com stopped waiting once the background app was running"

    # Capturing that run's output returns too: the background app lets go of
    # the pipe rather than holding it until it quits.
    $Capture = Start-Job { $Out = & $using:Launcher --daemon; $LASTEXITCODE }
    $Returned = Wait-Job $Capture -Timeout 10
    $App = Get-Process speecher -ErrorAction SilentlyContinue | Where-Object Path -eq $Exe
    if (-not $Returned) {
        $Capture | Stop-Job
        throw "Capturing the output of speecher.com --daemon was still waiting after 10 seconds"
    }
    $CaptureExit = Receive-Job $Capture
    if ($CaptureExit -ne 0) {
        throw "speecher.com --daemon exited with $CaptureExit when its output was captured"
    }
    if (-not $App) {
        throw "speecher.com --daemon returned its captured output but speecher.exe is not running"
    }
    Stop-BackgroundApp
    if (-not $App.WaitForExit(10000)) {
        throw "speecher.exe quit did not stop the background app started with captured output"
    }
    Write-Output "Capturing speecher.com --daemon's output returned while the background app ran"

    # Restart Manager closing the running app without forcing, the way Setup
    # does when it replaces files in use. Only the tray window answers it;
    # before it did, Speecher stayed running here even with a window open.
    Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class RestartManager {
    [DllImport("rstrtmgr.dll", CharSet = CharSet.Unicode)]
    static extern int RmStartSession(out uint session, int flags, StringBuilder key);
    [DllImport("rstrtmgr.dll")]
    static extern int RmEndSession(uint session);
    [DllImport("rstrtmgr.dll", CharSet = CharSet.Unicode)]
    static extern int RmRegisterResources(uint session, uint fileCount, string[] files,
                                          uint appCount, IntPtr apps, uint serviceCount, string[] services);
    [DllImport("rstrtmgr.dll")]
    static extern int RmShutdown(uint session, uint flags, IntPtr progress);

    public static int Shutdown(string path) {
        uint session;
        int error = RmStartSession(out session, 0, new StringBuilder(33));
        if (error != 0) {
            return error;
        }
        try {
            error = RmRegisterResources(session, 1, new[] { path }, 0, IntPtr.Zero, 0, null);
            return error != 0 ? error : RmShutdown(session, 0, IntPtr.Zero);
        } finally {
            RmEndSession(session);
        }
    }
}
"@
    $App = Start-Speecher "--daemon"
    $Shutdown = [RestartManager]::Shutdown($Exe)
    if ($Shutdown -ne 0 -or -not $App.WaitForExit(10000)) {
        throw "Restart Manager could not close the running application (error $Shutdown)"
    }
    Write-Output "Restart Manager closed the running application"

    # Installing over the running app: Setup asks it to quit before replacing
    # its files.
    $App = Start-Speecher "--show-settings"
    $Reinstall = Start-Process $InstallerPath -ArgumentList $Arguments -Wait -PassThru
    if ($Reinstall.ExitCode -ne 0) {
        throw "Reinstall over the running application exited with code $($Reinstall.ExitCode)"
    }
    if (-not $App.WaitForExit(10000)) {
        throw "Setup could not close the running application"
    }
    Write-Output "Setup closed the running application"
    if ((Get-PathEntryCount) -ne 1) {
        throw "Reinstalling left $(Get-PathEntryCount) entries for $InstallDir on the user's Path"
    }

    # Uninstalling under the running app must quit it rather than leave its
    # locked files, and the folder, behind. The empty folders stand in for
    # what an earlier interrupted uninstall leaves, which this install did
    # not create.
    New-Item -ItemType Directory -Force (Join-Path $InstallDir "leftover\nested") | Out-Null
    $App = Start-Speecher "--show-settings"
    Start-Process (Join-Path $InstallDir "unins000.exe") -ArgumentList "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART" -Wait
    if (-not $App.WaitForExit(10000)) {
        throw "Uninstall left the application running"
    }
    $Deadline = (Get-Date).AddSeconds(10)
    while ((Test-Path $InstallDir) -and (Get-Date) -lt $Deadline) {
        Start-Sleep -Milliseconds 250
    }
    if (Test-Path $InstallDir) {
        throw "Uninstall left files behind:`n$((Get-ChildItem $InstallDir -Recurse -Force).FullName -join "`n")"
    }
    Write-Output "Uninstall quit the running application and removed its folder"
    if ((Get-PathEntryCount) -ne 0) {
        throw "Uninstall left $InstallDir on the user's Path"
    }

    # A folder this install did not create that still holds a file stays.
    $Install = Start-Process $InstallerPath -ArgumentList $Arguments -Wait -PassThru
    if ($Install.ExitCode -ne 0) {
        throw "Second install exited with code $($Install.ExitCode)"
    }
    $Kept = Join-Path $InstallDir "keep\note.txt"
    New-Item -ItemType File -Force $Kept | Out-Null
    $Uninstaller = Join-Path $InstallDir "unins000.exe"
    Start-Process $Uninstaller -ArgumentList "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART" -Wait
    $Deadline = (Get-Date).AddSeconds(10)
    while ((Test-Path $Uninstaller) -and (Get-Date) -lt $Deadline) {
        Start-Sleep -Milliseconds 250
    }
    if (-not (Test-Path $Kept)) {
        throw "Uninstall removed a file it did not install"
    }
    Write-Output "Uninstall kept a file it did not install"
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
