# Speecher on macOS

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

The bundle's Info.plist carries the microphone and Apple Events usage
descriptions, so permission prompts work from a plain developer build. For a
distributable app, `make dmg` runs `macdeployqt` on a staged copy of the bundle.
Sign and notarize release builds as usual.

## Permissions Speecher asks for

| Permission | Why | When asked |
| --- | --- | --- |
| Microphone | Recording dictation | Setup assistant, or first dictation |
| Accessibility | Synthetic Cmd+V paste into the frontmost app, and reading the focused control for direct insertion | Setup assistant; grant needs an app restart to take effect |
| Automation (Music, Spotify, TV) | Optional pause/resume of playing media during dictation | First time media pause runs |
| Screen Recording | Optional target screenshots for refinement context | First capture; Speecher refuses to capture without it rather than sending a wallpaper-only image |

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
"Run setup assistant…", and wipe everything with
`defaults delete io.github.firemonster612.speecher`.

## What works on macOS that Wayland cannot offer

Linux/Wayland deliberately restricts global input and cross-window APIs
(see docs/research/0001-plasma-6-wayland-capabilities.md). macOS has
user-consented APIs for all of it:

1. **In-app global hotkeys with push-to-talk.** Carbon `RegisterEventHotKey`
   delivers both press and release, so tap-to-toggle and hold-to-talk work
   without binding anything in a desktop settings app. On Wayland the
   shortcut lives in the compositor and release events are only available
   through the GlobalShortcuts portal where implemented.
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
7. **Target screenshots without a per-shot dialog** once Screen Recording is
   granted; the portal on Wayland can prompt per capture.
8. **Learned corrections without polling.** An AXObserver reports the user's
   edit to the inserted text as it happens; AT-SPI has no text-change signal
   Speecher can rely on across toolkits, so on Linux the same span has to be
   re-read on a timer.

Known gaps on macOS: the ydotool/wl-copy output methods, which are Linux-only
by nature.

## VM testing note

The port was brought up in a QEMU macOS 26 (Tahoe) VM. Software-rendered
vibrancy makes WindowServer extremely slow there; on real hardware the
NSVisualEffectView sidebar and HUD popup composite normally. Eyeball the
chrome (sidebar blur, popup glass, traffic-light inset) on a physical Mac
before release.
