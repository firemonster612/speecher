# Speecher on Windows

Speecher supports Windows 11 build 22000 or newer.

`speecher.exe` is one process: the daemon, with the same Qt Quick UI as on
Linux inside it ([ADR 0009](adr/0009-one-quick-ui-inside-the-daemon.md)). Its
tray icon opens Speecher on a click and offers Open Speecher, Start or Stop
dictation, Settings and Quit in its menu. The dictation popup never takes
focus, so the app being dictated into keeps the keyboard. The command line
talks to the running app.

## Building

Install Visual Studio 2022 Build Tools with the MSVC x64 toolchain and Windows
11 SDK, CMake, Ninja, Git, Python, and Qt 6.9 or newer (CI uses 6.10.3)
`msvc2022_64` with Qt Multimedia and Qt WebSockets. In a developer PowerShell:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DSPEECHER_BUILD_TESTS=ON -DCMAKE_PREFIX_PATH=C:\Qt\6.10.3\msvc2022_64
cmake --build build
$env:QT_QPA_PLATFORM = "offscreen"
ctest --test-dir build --output-on-failure
```

Inno Setup 6 is also required to make an installer:

```powershell
pwsh packaging/windows/build-installer.ps1 -BuildDir build
```

The UI's own QML modules and fonts are compiled into `speecher.exe`
(`cmake/QuickUi.cmake`). The script runs `windeployqt` with `--qmldir` pointed
at the UI, so it deploys the Qt QML modules the UI imports, copies the Visual
C++ runtime DLLs from the MSVC redist folder next to `speecher.exe`, and writes
`dist\Speecher-Setup-x64.exe` plus its checksum. Run it from an MSVC developer
shell so that `VCToolsRedistDir` is set. Windows Update does not service these
app-local runtime copies; each Speecher release brings the build toolset's
current runtime.

`pwsh packaging/windows/verify-installer.ps1 -InstallerPath
dist\Speecher-Setup-x64.exe` installs it into a temporary folder and checks the
result, including that the installed app opens Main with only system folders
on Path. CI runs it after packaging.

Before a release, `packaging/windows/verify-installer-update.ps1` checks that
an installed copy of the last release updates to the new installer through its
own updater: the WinUI files gone; settings, both saved keys, the global
shortcut, the startup entry and Open With kept; and one Path entry. Run it on
a throwaway Windows machine, since it edits the hosts file and trusted roots;
the script's header says how.

## Installing

Run `Speecher-Setup-x64.exe`. The installer is per-user, needs no administrator
access, and installs to `%LOCALAPPDATA%\Programs\Speecher`.

The installer adds that folder to the user Path and uninstalling removes it, so
`speecher` runs in any Command Prompt or PowerShell window opened afterwards.

Both shells try `speecher.com` before `speecher.exe`. `speecher.exe` is a
GUI-subsystem program that the shells start without waiting, so its exit status
would be lost and the prompt would come back first; `speecher.com` is a small
console program that starts `speecher.exe` with the same arguments and standard
handles, waits for it, and returns its exit status. Ctrl+C reaches Speecher
itself rather than ending the launcher first. A run that opens a window or
starts the background app tells the launcher to stop waiting, so the prompt
comes back while Speecher keeps running. The Start menu shortcut and
Open with start `speecher.exe` directly, with no console window. Bind hotkeys
and GUI launchers such as the Run box or PowerToys Run to `speecher.exe` too: a
bare `speecher` finds `speecher.com` first and briefly opens a console window.
The build produces the launcher as `build\speecher-console.exe`; the installer
ships it as `speecher.com`.

Current releases are unsigned. If Microsoft Defender SmartScreen blocks a
browser download, choose "More info", check that the file came from the
Speecher GitHub release, then choose "Run anyway". Uninstall Speecher from
Windows Settings under Apps > Installed apps.

## Updates

The default Update Channel is Stable Release. You can select Nightly Build in
Settings > General > Updates.

Speecher downloads a new installer to
`%LOCALAPPDATA%\Speecher\updates\Speecher-Setup-<version>.exe`, checks the
SHA-256 value from `update-manifest.json`, and waits for the current Dictation
Session to finish. The installer then runs silently and starts the updated app.
If the updates folder is not writable, Speecher opens the GitHub release page
for a manual install.

## Desktop access and permissions

Windows asks for microphone access the first time Speecher records. UI
Automation and simulated paste do not have a separate consent prompt, but
Windows blocks a normal user process from reading or typing into an elevated
application. Run both applications at the same privilege level.

API keys use Windows Credential Manager through QtKeychain. Settings use the
current user's registry under
`HKCU\Software\io.github.firemonster612\speecher`. Logs are stored in the
application cache under `%LOCALAPPDATA%`.

The Windows port provides the in-app Global Shortcut, target detection and
text insertion, media pause and target screenshots. Windows-only gaps are
recorded in GitHub Issues.

## VM testing

The port's QEMU test machine is documented in
`.scratch/windows-port/vm/README.md`. Use its sync script for a clean MSVC build,
then run the installer in the interactive desktop session to check install,
update, relaunch, and uninstall behavior.
