# Unsigned macOS distribution with Sparkle EdDSA updates

Status: accepted, 2026-09-01

## Context

There is no Apple Developer account, so the DMG cannot be Developer-ID
signed or notarized. On macOS 15+ this means the first launch is blocked
by Gatekeeper until the user allows it in System Settings (the
right-click-Open bypass no longer works). Paying Apple $99/year was
rejected for now.

## Decision

Ship the DMG ad-hoc signed and not notarized, with the Gatekeeper
walkthrough documented in the README. Updates are handled by Sparkle 2,
which authenticates each update with the project's own EdDSA key
(SUPublicEDKey in Info.plist, private key only in CI secrets), so
update integrity does not depend on Apple signing. The channel setting
picks between two appcast feeds on gh-pages; there is no SUFeedURL in
the plist. Sparkle-installed updates carry no quarantine flag, so the
Gatekeeper wall is first-install only.

## Consequences

- First-install friction is real and documented; it is the cost of not
  paying Apple, not a bug.
- The EdDSA private key is the update trust root: if it leaks, an
  attacker who can also serve the appcast can push updates. Keep it a
  repo secret, rotate by shipping a new public key in a signed-by-old
  update.
- CFBundleShortVersionString carries the full nightly version string
  and would be rejected by App Store submission; deliberate, DMG-only.
- If a Developer ID is ever bought, only signing/notarization steps in
  CI change; Sparkle and the feeds stay as they are.
