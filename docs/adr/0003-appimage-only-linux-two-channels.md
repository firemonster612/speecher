# AppImage-only Linux packaging with two release channels

Status: accepted, 2026-09-01

## Context

Speecher's job is host integration: it launches `claude`/`codex` from the
user's PATH, reads their credential files, injects text via ydotool
(pkexec + /dev/uinput), and talks to AT-SPI and KGlobalAccel. A Flatpak
would need the sandbox opened so wide (host filesystem, host spawn,
device access) that the sandbox would be decoration, and Flathub review
would rightly balk. Flathub was the original plan; this is why it was
dropped.

## Decision

Linux ships as an AppImage only. Two Update Channels: Stable Release
(manual `vX.Y.Z` tag on master) and Nightly Build (rolling `nightly`
prerelease republished on every push to master). The channel is a
runtime user setting, default stable. The AppImage self-updates in-app
(full-file download, sha256 verify, atomic rename over $APPIMAGE) and
also embeds gh-releases-zsync metadata for external AppImage tools;
the embedded metadata follows the channel the image was built for,
while the in-app updater follows the setting. Version comparison uses
a monotonic build number (commit count on master), never string
comparison.

## Consequences

- Nightly history is not kept; the commit SHA in every version string
  is the way back to any given build.
- Flatpak remains possible later behind the same release assets, but
  would ship with its core text-injection feature degraded until the
  portal story covers it.
- CI must checkout with full history and tags (fetch-depth: 0) or
  build numbers stop being monotonic.

## Amendment 2026-09-03

CI builds the AppImage in a Debian trixie container and bundles the matching
Qt 6 and KDE stack. This sets glibc 2.41 as the Linux compatibility floor. The
Ubuntu 22.04 build used a separate Qt installation with no KDE Frameworks 6,
so it could not provide native Plasma styling, icons, Global Shortcuts, or the
KDE setup assistant.
