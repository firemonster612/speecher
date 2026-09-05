# Unpackaged WinUI 3 with a per-user Inno installer

Status: accepted, 2026-09-05

## Context

Speecher needs WinUI 3 for its Windows front end, but its shared application
core and event loop remain in Qt. MSIX would add package identity and cleaner
runtime dependency handling. It would also constrain process launch, file
access, and the desktop integration Speecher uses for dictation.

The project does not have a Windows code-signing certificate.

## Decision

The Windows build is an unpackaged, framework-dependent WinUI 3 application.
It ships the Windows App Runtime bootstrap DLL and winmd files beside
`speecher.exe`. A per-user Inno Setup installer puts the application in
`%LOCALAPPDATA%\Programs\Speecher` and installs Windows App Runtime 2.4.0 for
the current user. It does not require administrator access.

The installer and executable are unsigned. A browser download may trigger
Microsoft Defender SmartScreen on first install. Users must choose "More
info", then "Run anyway" until the project can sign releases.

Speecher checks the selected Update Channel using the same manifest as the
Linux updater. The manifest may contain both platform blocks:

```json
{
  "version": "0.1.4",
  "buildNumber": 412,
  "linux-x86_64": {
    "appimage": "https://github.com/firemonster612/speecher/releases/download/v0.1.4/Speecher-x86_64.AppImage",
    "sha256": "<64 lowercase hex characters>"
  },
  "windows-x86_64": {
    "installer": "https://github.com/firemonster612/speecher/releases/download/v0.1.4/Speecher-Setup-x64.exe",
    "sha256": "<64 lowercase hex characters>"
  }
}
```

Each updater reads only its own platform block. On Windows, Speecher downloads
the installer to `%LOCALAPPDATA%\Speecher\updates`, checks its SHA-256 value,
waits for an active Dictation Session to finish, starts Inno silently, and
quits. Inno closes the old process if needed, replaces the files, and starts
Speecher again.

## Consequences

- Installation, updates, settings, and credentials stay in the user's profile.
- Windows App Runtime remains a machine dependency serviced by Microsoft.
- The first browser download has real SmartScreen friction while releases are
  unsigned.
- Moving to MSIX later would require a new packaging and update path. The
  shared application core and WinUI front end would remain useful.
