# One settings navigation on every platform

Linux captures at 16f13291, offscreen with the KDE platform theme and Breeze in
an isolated config (`SPEECHER_GRAB_PAGE=<page id>`). `local-localModels.png` is
from a build with local speech on.

- `sidebar-separators.png`: the sidebar enlarged; a Breeze-drawn line between groups.
- `sidebar-dark.png`: the same in Breeze Dark.
- `sidebar-vs-systemsettings.png`: our sidebar beside KDE System Settings'.
- `refinement.png`: model, effort and fast mode now on Refinement.
- `shortcut.png`: the Global Shortcut recorder with Shortcut behavior under it.
- `vocabulary-corrections.png`: Vocabulary's three views as tabs (`vocabulary:corrections`).
- `whatsnew-1-pending.png`: What's New above Home while pending.
- `local-localModels.png`: Local models in the Dictation group.

Windows captures of the settings window at its normal size, with the update
banner up and What's New pending, from a CI runner at 16f13291 plus a
throwaway workflow change. The banner is a real offer: the updater read a
local manifest (a newer nightly build) through `SPEECHER_UPDATE_MANIFEST_URL`,
with automatic checks on and automatic install off, and the pending What's New
version was set in the registry settings.

- `windows-banner-home-light.png`, `windows-banner-general-light.png`: Home and
  General in light mode.
- `windows-banner-home-dark.png`, `windows-banner-general-dark.png`: the same in
  dark mode.
