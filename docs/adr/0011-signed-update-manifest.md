# Linux and Windows updates check an Ed25519 signature on the manifest

Status: accepted, 2026-11-14

## Context

The AppImage and the Windows installer update themselves from
`update-manifest.json`, a GitHub Releases asset that names the download and its
SHA-256. The hash came from the same release as the binary, so it proved the
download was intact, not that it was ours: anyone who could upload release
assets (a leaked token, a compromised account or CI job) could publish a
manifest and binary that every install with auto-install on would fetch and
run. macOS did not have this gap, because Sparkle checks an EdDSA signature
made with a key only CI holds (0004).

## Decision

The release signs the manifest with the Sparkle Ed25519 key pair, so no new
secret exists. `scripts/sign-update-manifest.py` signs in the publish job, after
the manifest has its final URLs, and writes `update-manifest.signed`: the base64
signature, a newline, then the manifest bytes unchanged. The signature covers
the fixed prefix `Speecher update manifest v1\n` followed by those bytes.
`ManifestUpdater` fetches only the signed file and checks the signature with
Monocypher against the public key compiled in from
`SPEECHER_SPARKLE_ED_PUBLIC_KEY`, before it parses a single field. A missing,
malformed or wrong signature fails the check; there is no unsigned path.

- **Domain separation.** Sparkle signs the raw bytes of a DMG. No DMG this
  release builds starts with the prefix, so a DMG signature never verifies as a
  manifest signature. The other way round, a file of prefix and JSON served in
  place of a DMG passes Sparkle's signature check but is no disk image, so
  there is nothing to install.
- **Key format.** `generate_keys -x` exports a 32-byte seed, or a 96-byte key
  from older Sparkle versions (the expanded private key, then the public key).
  Common Ed25519 libraries accept only the seed, so the script signs with the
  RFC 8032 arithmetic and refuses a key whose public half is not
  `SPARKLE_ED_PUBLIC_KEY`. Ed25519 is deterministic, so a committed fixture
  (`tests/data/update-manifest`) ties the script to the app's verifier: the
  publish test reproduces it byte for byte with the test key, and the
  updater's test verifies it.
- **Old installs.** `update-manifest.json` is still published, unsigned, for
  installs from before this change, which never fetch the signed file.
- **Builds without the key.** Pull-request and local builds have no key. Their
  update checks fail with "This build has no update signing key...", shown on
  the Updates page and logged at startup. They never fall back to an unsigned
  manifest. A release build without the key fails in CI: the AppImage package
  step, the Windows configure step and the publish step each refuse to run
  without it.

## Consequences

- The Sparkle private key is now the update trust root on all three platforms.
  Rotating it means shipping the new public key in an update signed with the
  old one on each platform.
- The signature proves origin, not freshness or channel. Someone with upload
  rights but no key can serve an older signed manifest, or one channel's
  signed manifest as the other's. Automatic checks only take a newer build, so
  the worst case is a signed nightly reaching stable users, or a stalled feed.
- Release-asset upload rights alone no longer let anyone run code on Linux and
  Windows installs from this version on. Installs from before it stay exposed
  until they update once.
