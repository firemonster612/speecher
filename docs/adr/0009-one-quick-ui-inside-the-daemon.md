# One Qt Quick UI inside the daemon

Status: accepted, 2026-10-24. Supersedes
[0001](0001-per-platform-front-ends.md) and
[0007](0007-presentation-lives-in-core.md), and the WinUI half of
[0005](0005-windows-winui3-inno.md).

## Context

ADR 0001 gave each platform its own front end: Qt Widgets on Linux, SwiftUI
on macOS and WinUI 3 on Windows, over a settings schema in core. ADR 0007
then moved every user-visible string into core presentation modules and
added wording lints, because three front ends kept drifting apart.

Both decisions existed to keep three UIs in agreement. The schema, the
presentation modules, the drift checks and the per-platform hosts grew into
a large share of the code, and each UI change still had to land three times.
A single Qt Quick UI (`src/frontend/quick/`) can look native enough on every
platform, so there is nothing left to keep in agreement.

## Decision

Speecher is three pieces that share state through one process:

- **The daemon** owns dictation, providers, recording, transcription,
  updates, the phone transfer server and the settings store
  (`ApplicationController`).
- **The CLI** is the same binary run with a verb. It talks to the running
  daemon over the local socket (`src/app/SingleInstanceIpc`), or runs a
  headless job (`transcribe`, `listen`, `providers`) on its own.
- **The UI** is the Qt Quick front end, shown by the daemon in its own
  process. `ApplicationController` reaches it through `AppFrontEnd`; with no
  UI attached the daemon runs without windows.

The Qt Quick UI replaces the Qt Widgets, SwiftUI and WinUI front ends on
every platform. The settings schema, the presentation modules that existed
only to feed the front ends, and the wording lints are removed. Core keeps
the wording the daemon and the CLI print themselves: errors, status lines,
`speecher providers` and `transcribe` output. A window's wording belongs to
the UI.

## Consequences

- A UI change is made once, in QML.
- Settings rows, page names and window captions are no longer described in
  core. The UI reads and writes `AppSettings` through the daemon, and names
  its own pages.
- Messages core writes still name UI pages by title ("the Local models
  page"), so renaming a page means updating those messages too.
- The Quick host (`src/frontend/quick/host`) runs on Linux, macOS and
  Windows, beside the tray icon (the menu bar item on macOS).
- Windows no longer needs the Windows App Runtime, and macOS no longer needs
  Swift.
