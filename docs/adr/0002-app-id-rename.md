# App id is io.github.firemonster612.speecher, renamed from local.speecher

Status: accepted, 2026-09-01

## Context

The original `local.speecher` id was a placeholder. No distribution channel
accepts it, the GlobalShortcuts portal requires a resolvable reverse-DNS
identity backed by an installed .desktop file, and Sparkle keys updates on
`CFBundleIdentifier`. Renaming after users exist breaks settings persistence,
macOS app identity (including TCC grants for microphone and accessibility),
and stored portal permissions all at once.

## Decision

Rename to the GitHub-derived id `io.github.firemonster612.speecher` before
publishing any artifact. QSettings organization becomes
`io.github.firemonster612`; the desktop file, icon, polkit action,
KGlobalAccel component, and `CFBundleIdentifier` all carry the full id. A
one-time startup migration copies QSettings keys from the old organization
(the only user state that exists; the cache directory holds a regenerable
log) and deliberately leaves the old file in place.

## Consequences

- The old settings file at `~/.config/local.speecher/speecher.conf` survives
  indefinitely and can contain API keys; a user purging secrets must delete
  it too.
- Old KGlobalAccel state under the `local.speecher` component is cleaned up
  via `cleanComponent` on the next Plasma run so the renamed component can
  re-register its default; a user's customised key sequence is not migrated.
- On macOS the `CFBundleIdentifier` change resets TCC permission grants for
  any pre-rename install.
