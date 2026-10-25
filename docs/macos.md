# Speecher on macOS

`speecher.app` is one process: the daemon, with the same Qt Quick UI as on
Linux inside it ([ADR 0009](adr/0009-one-quick-ui-inside-the-daemon.md)). Its
menu bar item offers Open Speecher, Start or Stop dictation, Settings and
Quit; a click on the Dock icon opens Speecher too, and audio opened with
Speecher in Finder opens its Transcribe page. The dictation popup is a
non-activating panel, so the app being dictated into keeps the keyboard. The
command line talks to the running app.

## Building

```sh
brew install cmake ninja pkgconf qt qtkeychain
make build
make test
make install
make dmg
open build/speecher.app
```

`make install` installs `speecher.app` in `/Applications` by default. `make dmg`
creates `build/speecher.dmg` with a drag-to-Applications layout.

The make targets run these CMake commands underneath:

```sh
cmake -S . -B build -G Ninja \
  -DCMAKE_PREFIX_PATH="$(brew --prefix qt);$(brew --prefix qtkeychain)"
cmake --build build
ctest --test-dir build --output-on-failure
cmake --install build --prefix /Applications
```

The UI needs Qt 6.9 or newer. Its own QML modules and fonts are compiled into
the `speecher` binary (`cmake/QuickUi.cmake`); Qt's QML modules load from Qt
at run time.

The bundle's Info.plist carries the microphone and Apple Events usage
descriptions, so permission prompts work from a plain developer build. For a
distributable app, `make dmg` runs `macdeployqt` on a staged copy of the
bundle, with `-qmldir` pointed at the UI so it bundles the Qt QML modules the
UI imports. `packaging/macos/verify-dmg.sh build/speecher.dmg` checks the app
in the image opens Main from the bundle alone; CI runs it after packaging.
Sign and notarize release builds as usual.

Echo cancellation for recordings needs webrtc-audio-processing 1.3, which
Homebrew does not package. Build it once into a prefix and point pkg-config at
it; `make dmg` bundles the library and its notices into the app:

```sh
brew install meson
# 15.0: Speecher's minimum macOS, CMAKE_OSX_DEPLOYMENT_TARGET in CMakeLists.txt
packaging/macos/build-webrtc-audio-processing.sh ~/webrtc-audio-processing 15.0
export PKG_CONFIG_PATH=~/webrtc-audio-processing/lib/pkgconfig
```

## Permissions Speecher asks for

| Permission | Why | When asked |
| --- | --- | --- |
| Microphone | Recording dictation | Setup assistant, or first dictation |
| Accessibility | Synthetic Cmd+V paste into the frontmost app, and reading the focused control for direct insertion | Setup assistant; grant needs an app restart to take effect |
| Automation (Music, Spotify, TV) | Optional pause/resume of playing media during dictation | First time media pause runs |
| Screen & System Audio Recording | Optional target screenshots for refinement context, and the other side of a call for `speecher record` | First screenshot; Speecher refuses to capture without it rather than sending a wallpaper-only image. For recordings, the first `record start`; without it a recording keeps the microphone alone, with echo cancellation off |

## After an update or rebuild

macOS ties every permission grant to the app's code signature. `make dmg`
signs ad hoc by default, and an ad-hoc signature is different on every build,
so installing an updated Speecher silently invalidates the old Accessibility
grant: System Settings keeps showing the toggle as on, while
`AXIsProcessTrusted` tells the new copy no. Two ways out:

- One-off: in System Settings > Privacy & Security > Accessibility, turn the
  Speecher toggle off and back on (or remove the entry and re-add it).
- Durable: sign with a stable identity so updates keep the grant. Create a
  self-signed code-signing certificate once in Keychain Access, then
  `SPEECHER_SIGN_IDENTITY="Your Cert Name" make dmg`.

Settings, including the "setup completed" flag, live in
`~/Library/Preferences` and survive reinstalling the app — deleting
speecher.app never resets them. Rerun the assistant from Settings > General >
Permissions & health > "Run setup", and wipe everything with
`defaults delete com.io-github-firemonster612.speecher`.

### CLI login Keychain prompts

For a self-signed app, Keychain checks a per-build `cdhash` partition in
addition to the stable certificate requirement. "Always Allow" on a direct
Speecher read therefore only approves that build; the next update prompts
again. This is separate from the Accessibility grant described above.

Speecher reads Claude Code and Codex Keychain entries through
`/usr/bin/security`, whose `apple-tool:` partition survives Speecher updates.
An entry that does not trust that tool can still ask for access. Keychain
operations time out after a few seconds.

Claude Code's entry is updated through the same `security` tool. Credentials
travel over stdin, and Speecher does not change the access list. The tool's
input buffer limits automatic refresh to documents whose hex-encoded update
command fits under 4096 bytes, roughly 2 KiB of credential data minus the
service and account names. Larger entries remain readable. Speecher checks the
compact JSON, including known output fields, before contacting OAuth and asks
you to run `claude` and `/login` if it cannot fit. It checks the final size too.
A provider response can contain larger tokens or scopes; if these no longer fit
after rotation, the write fails without truncation and you may need to sign in
again with the CLI.

Codex writes its Keychain entry through its native Rust keyring. Writing that
entry from another process can change its Keychain partition and disrupt
Codex's access. Speecher therefore only reads macOS Codex Keychain entries and
rejects automatic refresh before contacting OAuth. Run `codex login` to refresh
that entry. Codex file-based refresh and Windows Credential Manager refresh
remain supported.

For supported refreshes, Speecher checks that the entry still exists before
writing and rechecks the login after the OAuth request. These checks cannot
prevent every race with a CLI changing the entry at the same instant. Access
denial never selects a plaintext login. Codex uses the file first and Keychain
only when the file is absent, as described in the README.

Apple's [securityd partition selection](https://github.com/apple-oss-distributions/Security/blob/main/securityd/src/clientid.cpp)
distinguishes Apple tools, Apple-issued developer identities, and other
signed code; a stable self-signed certificate alone does not stabilize the
last category's partition.

## Release gate: updating SwiftUI users to the Qt Quick build

Releases up to 0.2.x shipped the SwiftUI front end. Those users get the Qt
Quick build as an ordinary Sparkle update, and their permissions, Keychain
items and login item carry over only if the new bundle is signed so macOS
takes it for the same app. Ad-hoc test builds can't show that: each has its
own signature. `packaging/macos/verify-dmg-update.py` drives the update
itself. It can't run unattended: 0.2.0 turns Sparkle's own scheduler off, so
Sparkle never downloads in the background, and the update waits for someone
to press Install and restart in the old app. Run the checklist below by hand
before the first Quick release, on an Apple Silicon Mac (the released DMGs are
arm64 only) kept for the purpose: the script trusts a throwaway CA in the
System keychain.

1. Get the shipped DMG from the latest GitHub release, not a rebuild. For the
   candidate, start `release.yml` by hand on the commit to release (Actions >
   Release > Run workflow): that run builds and signs the DMG as a release
   does, with `MACOS_SIGNING_IDENTITY` and the Sparkle key, and publishes
   nothing. Its `macos-release` artifact holds `speecher.dmg` and
   `sparkle-signature.txt`.
2. Compare the two bundles before installing anything, from each mounted DMG:
   - `codesign -dv --verbose=4 speecher.app`: the same `Identifier`
     (`io.github.firemonster612.speecher`) and `TeamIdentifier`, and neither
     says `Signature=adhoc`.
   - `codesign -d -r- speecher.app`: record both designated requirements.
     Then `codesign --verify -R="<the shipped app's requirement>"
     candidate/speecher.app` must pass. macOS checks grants and Keychain
     access against the shipped app's requirement.
   - `plutil -extract SUPublicEDKey raw` and `CFBundleIdentifier`,
     `CFBundleExecutable` match in both `Info.plist`s, and the candidate's
     `CFBundleVersion` is higher.
3. Set up the shipped app as a user would: copy it to `/Applications`, run
   the setup assistant, grant Microphone, Accessibility and Input Monitoring,
   turn on Start Speecher at login, save a dummy OpenAI key in Accounts, and
   dictate once into TextEdit with the shortcut. Then give it the rest of what
   a user may have granted: dictate while Music plays and allow the
   Automation prompt (media pause); turn on screenshot context for ChatGPT
   refinement and allow Screen & System Audio Recording at the first capture;
   sign in to Claude Code (`claude`) and Codex (`codex login`), whose Keychain
   items Speecher reads, and choose each once as a provider so Speecher reads
   them.
4. Run `SPEECHER_DISPOSABLE_MACHINE=1 packaging/macos/verify-dmg-update.py
   shipped.dmg candidate.dmg sparkle-signature.txt evidence --keep-state`. It
   records the settings as you left them and compares the new app's against
   them, changed only where the update changes them on purpose. In the old
   app's settings, press Check now beside Check for updates, then Install and
   restart when it offers the candidate. The script must report every check as passed.
5. After the relaunch, without touching System Settings:
   - No permission prompt appears, and `speecher.log` reads
     `macOS accessibility identity ... trusted=1`.
   - The shortcut starts dictation (Input Monitoring), the microphone records,
     and the text pastes into TextEdit (Accessibility).
   - Music pauses while you dictate and resumes after, with no Automation
     prompt.
   - A dictation with screenshot context on refines with no Screen & System
     Audio Recording prompt, and System Settings still lists Speecher there,
     on.
   - Accounts shows the OpenAI key as saved, and Claude and ChatGPT as signed
     in, with no Keychain prompt and no request to sign in again.
   - System Settings > General > Login Items lists Speecher once.
   - Quit it and start it from Finder or Launchpad, which is how LaunchServices
     starts it at login too: its window takes clicks and typed keys. On a
     test VM a copy LaunchServices started took no input; no real Mac has
     checked this yet.
6. Log out and back in: Speecher starts by itself.
7. Repeat steps 3 to 5 with Accessibility turned off before the update: the
   new app must say the grant is missing rather than treat setup as done.
8. Keep both `codesign` outputs, both requirements and the script's evidence
   folder with the release.

If step 2 fails, the shipped build's signature can't carry grants over. Say
so in the release notes, telling users to grant the permissions again. No
migration can move TCC records.

## The speecher command

The CLI binary lives inside the bundle, at
`speecher.app/Contents/MacOS/speecher`, which is not on the PATH. The menu bar
panel's **Install command line tool…** went with the SwiftUI front end; until
the Qt Quick UI offers it again, write `/usr/local/bin/speecher` yourself, a
two-line script that runs the bundle's binary with your arguments. The script
points at the app where it is now, so after moving the app, write it again.
For an app in `/Applications`:

```sh
sudo mkdir -p /usr/local/bin
sudo rm -f /usr/local/bin/speecher
sudo tee /usr/local/bin/speecher >/dev/null <<'EOF'
#!/bin/sh
exec '/Applications/speecher.app/Contents/MacOS/speecher' "$@"
EOF
sudo chmod 755 /usr/local/bin/speecher
```

A script rather than a symlink: the binary has to run from inside its bundle
to find its frameworks and plugins.

## What works on macOS that Wayland cannot offer

Linux/Wayland deliberately restricts global input and cross-window APIs
(see docs/research/0001-plasma-6-wayland-capabilities.md). macOS has
user-consented APIs for all of it:

1. **In-app global hotkeys with push-to-talk.** Carbon `RegisterEventHotKey`
   delivers both press and release, so tap-to-toggle and hold-to-talk work
   without binding anything in a desktop settings app. On Wayland the
   shortcut lives in the compositor: Speecher gets hold-to-talk on Plasma and
   GNOME 48+, which forward key release through the GlobalShortcuts portal and
   KGlobalAccel, and falls back to tap-to-toggle on backends that report only
   the press.
2. **Target identification.** `NSWorkspace.frontmostApplication` plus the
   Accessibility focused element give Speecher the app, window title, and
   focused control every time. Wayland has no foreign-window API.
3. **Synthetic paste with no daemon.** One CGEvent Cmd+V replaces the whole
   ydotool/uinput/polkit setup chain; nothing to install.
4. **Direct insertion with verification.** Setting `AXSelectedText` on the
   focused element inserts at the caret and the result can be read back —
   the receipt honesty AT-SPI only delivers on cooperative toolkits.
5. **A dictation popup that never steals focus, on every desktop.** A
   non-activating NSPanel at status-window level joins all Spaces; on Linux
   this needed the Wayland-only layer-shell protocol.
6. **Reliable clipboard snapshot and restore** through one API that all
   applications share.
7. **Target screenshots without a per-shot dialog** once Screen & System Audio
   Recording is granted; the portal on Wayland can prompt per capture.
8. **Learned corrections without polling.** An AXObserver reports the user's
   edit to the inserted text as it happens; AT-SPI has no text-change signal
   Speecher can rely on across toolkits, so on Linux the same span has to be
   re-read on a timer.

Known gaps on macOS: the ydotool/wl-copy output methods, which are Linux-only
by nature.

## VM testing note

The Qt Quick UI was brought up in a QEMU macOS 26 (Tahoe) VM, which renders
in software: Main and the popup draw slowly there, and their shadows and
animations need checking on a physical Mac before release.
