# One settings navigation on every platform

The sidebar on every platform reads one arrangement from core: an untitled top
group (Home, General, Accounts, with What's New first while pending or open),
then Speech (Dictation, Local models, Transcribe) and Text (Refinement,
Vocabulary, Output). Dictation leads with the Global Shortcut section; Output
holds the app rules as sections.

## Linux

Captured at bfa34d63, offscreen with the KDE platform theme and Breeze in an
isolated config (`SPEECHER_GRAB_PAGE=<page id>`), from a build with local
speech on, at the window's default size unless noted.

- `linux-sidebar-headers.png`: the sidebar enlarged; each group titled like
  Kirigami's ListSectionHeader: a bold title starting at the icons and a
  Breeze-drawn line.
- `linux-general-whatsnew-light.png`, `linux-general-whatsnew-dark.png`: What's
  New pending, first in the top group.
- `linux-general-light.png`, `linux-general-dark.png`: nothing pending.
- `linux-sidebar-vs-systemsettings-light.png`,
  `linux-sidebar-vs-systemsettings-dark.png`: our sidebar beside KDE System
  Settings'.
- `linux-dictation-light.png`, `linux-dictation-dark.png`: Dictation with the
  Global Shortcut recorder and Shortcut behavior at the top.
- `linux-output-light.png`: Output, whose app rules are now sections.
- `linux-refinement.png`, `linux-vocabulary-corrections.png`,
  `linux-local-models.png`: other panes at 1000×760.

## Windows

The settings window at its normal size on a CI runner at 26323bc8, with the
dark-card fix from `fix/windows-dark-cards` applied for these captures. The
update banner is a real offer: the updater read a local manifest (a newer
nightly build) through `SPEECHER_UPDATE_MANIFEST_URL`, with automatic checks on
and automatic install off; the pending What's New version was set in the
registry settings.

- `windows-banner-home-light.png`, `windows-banner-home-dark.png`
- `windows-banner-general-light.png`, `windows-banner-general-dark.png`
- `windows-dictation-light.png`, `windows-dictation-dark.png`: the recorder at
  the top of Dictation.

## macOS

From the PR's CI at 26323bc8.

- `macos-home.png`, `macos-general.png`: screen captures from the insights
  evidence job, with the native Speech and Text sidebar sections.
- `macos-dictation.png`: the setup E2E's backing-store grab of Dictation, with
  the recorder at the top (the backing store leaves the sidebar blank).
