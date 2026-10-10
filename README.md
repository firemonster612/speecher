<p align="center">
  <img src="packaging/io.github.firemonster612.speecher.svg" alt="Speecher icon" width="96" height="96">
</p>

<h1 align="center">speecher</h1>

<p align="center">Speech-to-text for Linux, macOS, and Windows that reuses your existing subscriptions.</p>

## Quick start

### Prerequisites

Sign in to at least one transcription service: Claude Code for Claude Voice, or the ChatGPT app or Codex CLI for ChatGPT Codex dictation. Speecher can refresh expired OAuth logins and save rotated tokens back to the selected store. macOS Codex Keychain logins and oversized Claude Keychain entries require the owning CLI to refresh them.

```sh
# Arch
sudo pacman -S cmake ninja gcc qt6-base qt6-multimedia qt6-websockets qt6-wayland layer-shell-qt qtkeychain-qt6 wl-clipboard at-spi2-core kglobalaccel kwidgetsaddons kcolorscheme libpulse

# Debian
sudo apt install cmake ninja-build g++ qt6-base-dev qt6-multimedia-dev qt6-websockets-dev qt6-wayland liblayershellqtinterface-dev qtkeychain-qt6-dev wl-clipboard libatspi2.0-dev libkf6globalaccel-dev libkf6widgetsaddons-dev libkf6colorscheme-dev libpulse-dev

# Fedora
sudo dnf install cmake ninja-build gcc-c++ qt6-qtbase-devel qt6-qtmultimedia-devel qt6-qtwebsockets-devel qt6-qtwayland layer-shell-qt-devel qtkeychain-qt6-devel wl-clipboard at-spi2-core-devel kf6-kglobalaccel-devel kf6-kwidgetsaddons-devel kf6-kcolorscheme-devel pulseaudio-libs-devel

# macOS
brew install cmake ninja pkgconf qt qtkeychain
```

On Linux the libpulse development package enables system audio capture; a
build without it leaves system audio out.

On Windows 11, install the MSVC 2022 build tools and Qt 6.8.3, then follow
[`docs/windows.md`](docs/windows.md). Windows release builds install from
`Speecher-Setup-x64.exe`.

### Install

```sh
make install
```

The Makefile detects Linux and macOS; pass `PLATFORM=linux` or
`PLATFORM=macos` to be explicit. Windows build commands are in
[`docs/windows.md`](docs/windows.md).

On Linux this installs to your per-user prefix, `~/.local`:

- binary: `~/.local/bin/speecher`
- desktop file: `~/.local/share/applications/io.github.firemonster612.speecher.desktop`
- icon: `~/.local/share/icons/hicolor/scalable/apps/io.github.firemonster612.speecher.svg`

To install somewhere else:

```sh
make install PREFIX=/usr/local
```

On macOS `make install` puts `speecher.app` in `/Applications`. First launch runs a setup assistant that walks the microphone and Accessibility grants; Speecher restarts itself when setup finishes so macOS hands it the permissions.

Portable packages:

- Linux: `make appimage` → `dist/Speecher-x86_64.AppImage`
- macOS: `make dmg` → `build/speecher.dmg`, a drag-to-Applications disk image with Qt bundled
- Windows: `pwsh packaging/windows/build-installer.ps1 -BuildDir build` → `dist\Speecher-Setup-x64.exe`

## Installation & updates

On Linux, download the AppImage, make it executable, and run it:

```sh
chmod +x Speecher-x86_64.AppImage
./Speecher-x86_64.AppImage
```

It updates itself from inside the app. External update tools use the zsync metadata embedded for the channel this AppImage was built for, even if you switch the in-app Update Channel.

On macOS, open the DMG and drag `speecher.app` to Applications. On macOS 15 and later, Gatekeeper blocks the first launch. Choose **System Settings > Privacy & Security > Open Anyway**, or run:

```sh
xattr -dr com.apple.quarantine /Applications/speecher.app
```

The app is not Developer ID signed or notarized because there is no Apple Developer account. Releases use one stable self-signed identity so Accessibility grants survive updates. Sparkle verifies later updates with EdDSA signatures.

On Windows, run `Speecher-Setup-x64.exe`. The unsigned installer may show a
Microsoft Defender SmartScreen warning after a browser download. Choose
**More info > Run anyway** after checking that the file came from the Speecher
GitHub release. The installer is per-user and needs no administrator access.
Speecher downloads later installers in-app, verifies their SHA-256 values, and
runs them silently after the active Dictation Session finishes.

The default Update Channel is Stable Release. Nightly Builds are republished from every push to `master`, not on a nightly schedule. Switch channels in **Settings > General > Updates**.

### Global shortcut

On macOS, Speecher registers its own global hotkey — set it in the setup assistant or Settings, including press-and-hold push-to-talk. Nothing to configure outside the app.

On KDE Plasma and desktops with a Global Shortcuts portal, Speecher registers the shortcut automatically. Set it in the setup assistant or Settings. Holding the shortcut dictates and releasing it stops, the same press-and-hold push-to-talk as macOS, on any backend that reports key release (Plasma and GNOME 48+ do). A backend that reports only the press keeps the shortcut a plain tap-to-toggle.

If your desktop does not support either method, bind a key to the CLI manually:

```sh
/path/to/speecher toggle
```

If you installed with the default `make install`, the command is:

```sh
~/.local/bin/speecher toggle
```

If you installed an AppImage:

```sh
/path/to/Speecher-x86_64.AppImage toggle
```

On Plasma, the manual fallback is:

1. Open `System Settings > Keyboard > Shortcuts`.
2. Select `Add New > Command or Script`.
3. Set the command to `/path/to/speecher toggle`.
4. Click `Add`.
5. Assign your preferred shortcut under `Custom Shortcuts`.

Speecher also has separate `start` and `stop` commands, for press-and-hold on desktops the portal and KGlobalAccel don't cover. Bind the key-press action to `speecher start` and the matching key-release action to `speecher stop` in whichever Plasma shortcut tool or input remapper you use.

Add `--format html` or `--format plain` to `toggle` or `start` when you want a shortcut that overrides the saved output format for one dictation.

Add `--profile <name>` to `toggle` or `start` to use one Writing Profile for that dictation, whichever app you dictate into. Give the profile's current name in any case, quoted if it has spaces, or with `-` between its words: `--profile email`, `--profile "AI coding"` or `--profile ai-coding`, and `--profile stand-up` for one of your own named "Stand up". `speecher --help` lists the names your settings offer. If one of your profiles shares a name with another, rename one of them.

Add `--language <code>` to `toggle` or `start` to dictate one session in another Spoken Language, such as `--language de` for German or `--language auto` to let the speech service detect it. The code must be one the chosen speech service or Local Model lists under Spoken Language on the Dictation page. The options combine, and none of them changes your settings.

## Android

Speecher for Android is a dictation keyboard with a button that docks beside your usual keyboard. Tap the button, speak, then insert the text into the field you were typing in. It transcribes with your own ChatGPT or Claude account, signed in on the phone. It is not on the Play Store.

1. Download `Speecher-<version>.apk` from the newest [`android-v*` release](https://github.com/firemonster612/speecher/releases) on the phone and open it. Allow your browser to install unknown apps when Android asks.
2. Open Speecher and work through the setup list: sign in, allow the microphone, turn on the Speecher keyboard, and turn on the dictation button.
3. Android may block the dictation button, because it is an accessibility service in a sideloaded app. If it does, open **App info** for Speecher, tap the three-dot menu, choose **Allow restricted settings**, and turn the button on again.

No computer or adb is needed. When a new version is out, Home shows an update row. The first update asks you to let Speecher install apps. After that, Android installs Speecher's updates without asking again.

## Build

```sh
make build
make test
```

or

```sh
cmake -S . -B build -G Ninja -DSPEECHER_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

On macOS the Makefile adds the Homebrew Qt paths for you; the raw CMake equivalent is in `docs/macos.md`.
Windows prerequisites, build commands, and installer details are in
[`docs/windows.md`](docs/windows.md).

Required Qt modules are Core, Widgets, Network, and Multimedia. Linux builds require the KDE GlobalAccel, WidgetsAddons, ColorScheme, and LayerShellQt development packages by default. Pass `-DSPEECHER_WITH_KDE=OFF` only for a reduced development build without the complete Plasma integration.

CMake prints a `Speecher feature summary` for KDE, LayerShellQt, Qt WebSockets, QtKeychain, AT-SPI, and the Wayland helper. A missing optional dependency appears as `0` there and omits that integration. Release AppImages use `SPEECHER_RELEASE_BUILD=ON`, which rejects every missing release integration. AT-SPI supplies target discovery, context, and paste verification on Linux; macOS uses the Accessibility API instead.

Local speech (transcribe.cpp) is off in a default build because it more than
doubles a clean build. Turn it on with `-DSPEECHER_WITH_LOCAL_SPEECH=ON`; that
compiles one CPU backend tuned for your machine. Release packages also pass
`-DSPEECHER_LOCAL_SPEECH_ALL_CPUS=ON` so one package runs on any x86-64 CPU, and
`SPEECHER_RELEASE_BUILD=ON` turns both on. Custom endpoints and local runners
such as Ollama need no build option.

## AppImage

Build a portable AppImage with:

```sh
packaging/build-appimage.sh
```

The script creates `dist/Speecher-x86_64.AppImage`. It uses CMake install output and `appimagetool`. If `wl-copy` is installed on the build machine, it is bundled into the AppImage by default; pass `--no-bundle-wl-clipboard` to keep wl-clipboard external.

Requirements: Speecher's AppImage requires glibc 2.43 or newer (Debian 14, Fedora 44, Ubuntu 26.04, or later).

## DMG

```sh
make dmg
```

builds `build/speecher.dmg`: a copy of the app run through `macdeployqt` so Qt travels inside the bundle, plus an `/Applications` symlink in the classic drag-install layout. The script is `packaging/macos/build-dmg.sh`.

## Run

```sh
./build/speecher            # Linux
open build/speecher.app     # macOS
.\build\speecher.exe         # Windows PowerShell
./build/speecher toggle
./build/speecher start
./build/speecher stop
./build/speecher status
./build/speecher status --watch
./build/speecher last
./build/speecher vocabulary add FileTranscriptionSession "Speecher CLI"
./build/speecher --version
```

These CLI commands contact the running app through a per-user socket (on macOS the binary lives at `build/speecher.app/Contents/MacOS/speecher`). `toggle` switches recording on or off, `start` only starts it, `stop` only stops it, and `status` prints the current state. If `toggle` or `start` can't find a running instance, it starts a popup-only background process and begins listening. Calling `stop` or `status` without a running instance prints `idle`. `status --json` prints one object, `{"state": "idle", "recording": false}`, where `recording` is the object `record status --json` prints while a recording runs. `status --watch` is for status bars such as Waybar, tmux and SketchyBar: it prints a line at once and another whenever the dictation state changes or a recording starts, stops, or has a stream reconnect or stop, until Ctrl-C ends it with status 0. A line is the state, such as `idle` or `listening`, or while recording `recording 00:12:03`, after the state unless that is idle, and the duration is the recording's at that change. With `--json` each line is the object `status --json` prints. Started before Speecher, it prints `idle` and looks for Speecher every 2 seconds; when Speecher quits, it exits with status 0. A Speecher too old for `--watch` makes it say so and exit with 1. `last` prints the running app's last transcript, the text the tray panel and Home show, which helps when a paste landed in the wrong window. It is kept in memory only, so it prints nothing and exits with status 1 when there is no transcript yet or no running app. `vocabulary add <terms...>` saves the terms to the custom vocabulary, where Settings > Vocabulary and the next dictation have them without a restart. A term already in the list, in any case, is skipped and named on stderr. Put `--` before terms that start with `-`, as in `vocabulary add -- -fsanitize`. With no running app it saves them to the settings directly. It exits with status 0 when every term was added or already there, 1 when the settings could not be saved, and 2 for a usage mistake.

### Transcribing audio files

`speecher transcribe memo.wav` (or opening an audio file with Speecher from a file manager) opens a small Transcribe window with the file listed. Any option below runs it without a window instead: the files are transcribed in the calling process, progress goes to stderr, and each transcript is saved as `<name>-transcribed.txt` next to its audio file. It uses its own provider connections, so it never interrupts a running Speecher's dictation.

```sh
speecher transcribe --headless memo.wav                  # settings' choices, save beside
speecher transcribe --refine none --stdout *.m4a         # raw speech, printed too
speecher transcribe --profile email --output ~/notes talk.mp3
speecher transcribe --json --output none a.wav b.wav     # one JSON object per file, then a summary
speecher transcribe --model local --srt talk.mp3         # talk-transcribed.srt subtitles
ffmpeg -i talk.mp4 -f wav - | speecher transcribe --stdout -   # audio from stdin
```

`--model` picks the speech provider, `--refine` the refinement provider (or `none`), `--cleanup` the cleanup level (`none`, `light`, `medium`, `high`), `--profile` a writing profile whose saved cleanup and tone seed the run, `--tone` a tone, and `--language` the Spoken Language (a code such as `de`, or `auto`). `--no-vocabulary` skips custom vocabulary, and `--vocab-file <path>` adds the file's terms to it for this run only, one per line, skipping blank lines and lines starting with `#`, so a coding agent can pass identifiers from the repo it works in. They go to the speech service and to refinement ahead of your saved terms, in the file's order, as many as each takes, even with `--no-vocabulary`. Your settings are not changed. `--raw` prints and saves the unrefined transcript. `--srt` or `--vtt` saves and prints SRT or WebVTT subtitles instead of text, the same file the Transcribe window's Export writes, and with `--json` each object's `text` holds the subtitles. Subtitles come from the raw transcript and need timings, which only Local models return; with any other speech provider each file fails. Unset choices come from your settings. `-` as the only file reads the audio from stdin, so `ffmpeg` or `sox` can pipe it in; the audio is spooled to a temporary file that is removed when the run ends, even when a signal interrupts it; on Windows, removal when the run is interrupted is best effort and can leave the file in the temporary folder. Stdin must be a pipe or file, not a terminal. It always runs without a window, needs `--stdout`, `--json` or `--output <folder>`, and is named `stdin` in progress and saved as `stdin-transcribed.txt`; with `--json` its `file` is `-`. Empty stdin fails the run with status 1. The exit status is 0 when every file was transcribed and saved, 1 when any failed or could not be saved, and 2 for a usage error. `speecher --help` lists every option. The run reads the same settings and sign-in stores as a running Speecher, the way the Codex and Claude CLIs share theirs, so a token it refreshes is the one the app uses next.

On Linux, Speecher uses one window with a KDE-style sidebar, searchable settings pages, and dictation controls; `speecher settings` opens it on General settings. On macOS, Speecher is a menu bar app: dictation lives in the menu bar item and a floating panel, and settings open in a native window from the menu bar, the Dock, or ⌘,.
On Windows, Speecher uses a WinUI 3 settings window, a notification-area icon,
and a non-activating dictation panel.

### Voice input from the terminal

`speecher listen` records from the microphone once and prints the transcript to stdout, so a script or a coding agent can take spoken input. Progress and errors go to stderr. Ctrl-C, or Enter when stdin is a terminal, stops the recording and keeps what was said; `--until-silence [seconds]` also stops it after that much silence once you have started speaking (2 seconds when no number is given). An agent has no key to press, so it needs `--until-silence`. Piped stdin is left unread, so `listen` can run inside a `while read` loop.

```sh
git commit -m "$(speecher listen --until-silence)"
speecher listen --until-silence 2 --refine none   # no refinement
speecher listen --json --profile ai-coding        # one JSON object with the text
```

It takes the same `--model`, `--refine`, `--cleanup`, `--profile`, `--tone`, `--language`, `--no-vocabulary`, `--vocab-file`, `--raw` and `--json` choices as `transcribe`, with unset ones from your settings. Like `transcribe`, it runs in the calling process with its own microphone and provider connections, so it works while Speecher is running and even while it is dictating. Silence is judged by the Skip silence threshold in your microphone settings. The exit status is 0 when a transcript was printed, 1 when it failed or heard no speech, and 2 for a usage error.

### Checking providers

`speecher providers` lists every speech and refinement provider this build offers, with whether it is configured, signed in and usable, so a script or agent can check before calling `listen`. `--json` prints a JSON array of objects with `id`, `role`, `label`, `signsIn`, `configured`, `signedIn`, `usable` and `problem`.

```sh
speecher providers
speecher providers --json | jq '.[] | select(.usable == false)'
```

It judges each provider the way the Dictation and Refinement settings do and words a problem as their rows do ("No server URL is set."). While Speecher is running it asks Speecher, which knows the sign-ins it has seen and the Local Runners it has looked for. Otherwise it judges from your settings, the downloaded Local Models and the system's network state. It makes no network calls, reads no keyring and refreshes no sign-in, so a sign-in nothing has checked yet, and whether a Local Runner is running, read as `Unknown` in the table and `null` in JSON. A provider that doesn't sign in has `signsIn` false, `signedIn` `null` and `-` in the table.

### Recording a call

`speecher record start` records the microphone into a transcript file that a coding agent can follow with `tail -f`, and prints the file's path. The recording runs in the Speecher app, which `record start` starts when it isn't running; dictation keeps working meanwhile. Each utterance is appended as one line once you pause, or after 25 seconds of unbroken speech, and the provider has finished its text. The time is when that text was finished, counted from the start of the recording:

```
[00:12:09] me: Yes, I'll check it today.
```

```sh
speecher record start                  # recordings/<yyyy-mm-dd-hhmm>.md in Speecher's data folder
speecher record start --to call.md     # or this file
speecher record status                 # path, duration and streams; --json for one object
speecher record stop                   # writes the last utterance, then prints the path
```

Speecher's data folder is `~/.local/share/io.github.firemonster612/speecher` on Linux. An existing file is never overwritten: the recording goes to `call-2.md` and so on instead. Files are kept until you delete them. `--vocab-file <path>` adds the file's terms to the custom vocabulary for this recording only, read the same way as for `transcribe`; an unreadable file is a usage error (exit 2). The tray icon and its tooltip show that a recording is running. The first `record start` prints a reminder that recording other people may need their consent.

Recording needs Claude Voice, ChatGPT Codex or a Custom Endpoint; Local Models can't record yet. `record start` prints the path once the provider is connected. When it can't connect, for example when you are signed out or offline, or the provider refuses a second session while you dictate, `record start` says why, leaves no file and exits with 1. If the stream drops later, Speecher reconnects after 1, 2, 5 and 10 seconds, then every 30 seconds, for as long as the recording runs, and `record status` says it is reconnecting. What you say meanwhile is sent once the stream is back, up to the last 10 minutes of it. The words the provider was still working on when the stream dropped are written as a line as they stood, since it will never finish them. Before each new stream Speecher renews the sign-in when it is due, and once more when the provider turns it down. A failure a reconnect can't mend, such as being signed out, stops the stream: the recording stays open and `record status` says why, on stdout and stderr, until you stop it. `record stop` still prints the path when the recording missed something, such as lines it could not write to the file, says what on stderr, and exits with 1. `record status` exits with 1 when nothing is recording. Only the microphone is recorded for now; `--mic-only` is accepted and changes nothing.

A Custom Endpoint has no stream to connect, so `record start` prints the path at once. Each utterance is uploaded on its own when it ends, after the one before has been transcribed, with your vocabulary and the end of the text before it as the prompt. The quiet between utterances is not uploaded, except the 300 ms before each, so a recording that hears no speech uploads nothing. An utterance the endpoint fails to transcribe because it is down, slow or busy is missing from the file: `record status` says so, the recording goes on with the next, and `record stop` exits with 1. Any other failure, such as a refused key, a wrong path or an account out of quota, would fail every utterance, so it stops the stream at once, with no retry, as there is no sign-in to renew: `record status` says why and `record stop` exits with 1. With a Custom Endpoint, `record stop` waits up to 65 seconds for each utterance still to be transcribed, rather than the 15 seconds other providers get, as a slow server may say nothing for a minute while it works, and three minutes in all.

## Uninstall

Remove Speecher's shortcut in your desktop's keyboard settings. If you added an AppImage to the app menu, delete `~/.local/bin/speecher`, `~/.local/share/applications/io.github.firemonster612.speecher.desktop`, and `~/.local/share/icons/hicolor/scalable/apps/io.github.firemonster612.speecher.svg`.

On Windows, uninstall Speecher from **Settings > Apps > Installed apps**.

## Transcription

Choose Claude Voice or ChatGPT Codex under the transcription settings. The setup assistant reads the same provider registry, so newly registered transcription services appear in both places without separate wizard changes.

ChatGPT Codex dictation uses the same streaming protocol as the Codex CLI and reuses its ChatGPT OAuth session. An OpenAI API key cannot authorize this endpoint. Sign in with `codex login` or the ChatGPT app before using it.

Speecher reads `auth.json` inside `CODEX_HOME`, or `~/.codex` when unset. If the
file is absent, it reads Codex's native login from macOS Keychain or Windows
Credential Manager. An unreadable or malformed file does not select the native
login. This file-first policy is independent of Codex's `cli_auth_credentials_store`
configuration. Linux currently supports the file store.

Native entries use service `Codex Auth` and account `cli|` followed by the first
16 SHA-256 hex characters of the canonical Codex home path. On Windows, the
target is `<account>.Codex Auth` and the password blob is UTF-16LE. OAuth refresh
writes back to the selected file or Windows Credential Manager entry and
preserves unknown fields. It checks for a login or logout that happened during
the OAuth request before writing. A native refresh never creates an `auth.json`
file.

On macOS, Speecher only reads Codex Keychain entries. Refresh them with
`codex login`; Speecher rejects automatic refresh before contacting OAuth so it
does not change the native entry's ownership. File-based Codex refresh still
works. See [macOS Keychain details](docs/macos.md#cli-login-keychain-prompts).

Refresh preflight checks the compact document and known output fields against
store limits. A provider can still return larger tokens. If the final document
no longer fits after token rotation, sign in again with the owning CLI.

Native binaries use one stable user socket, so the desktop app and CLI shortcut talk to the same instance after `make install`. AppImages have their own stable socket because their internal mounted path changes on each launch.

## Refinement

OpenAI and Anthropic refinement can be tuned in Settings. On first run, Speecher defaults refinement to OpenAI when the Codex CLI is installed; if Codex is not installed but Claude Code is installed, it defaults to Anthropic. Explicit provider choices in Settings are preserved. The default style is `Balanced` with adaptive Markdown-compatible output. Refinement is built from composable rules: always-on preservation rules, cumulative level rules, output-style rules, and conflict-resolution rules.

Anthropic refinement defaults to Claude Opus 5.5. The Settings model picker shows one simple display name per built-in Claude model, such as Claude Opus 5.5, Claude Opus 5, Claude Sonnet 5.5, and Claude Haiku 4.5. It intentionally excludes Fable 5 and Mythos 5 after their June 13, 2026 access suspension. The field is editable for newer or account-specific model IDs. Speecher warns when a Haiku model is selected because Haiku has been observed interpreting the transcript being refined as instructions.

Refinement effort is configurable per provider. OpenAI effort maps to `reasoning.effort` on the Responses API and defaults to `none` with GPT-6 Luna. Anthropic effort maps to Claude Code's interactive `--effort` flag in Claude Code session mode, and to Anthropic adaptive thinking plus `output_config.effort` in OAuth extra usage mode when the selected model supports it. Anthropic effort defaults to `low`.

Settings also includes output controls for choosing how Speecher delivers text, including setup for typing directly into focused text fields. When virtual-keyboard paste is enabled, Speecher can optionally restore the previous clipboard contents after delivery.

Target context uses the focused application's accessibility data when it is available: app identity, control role, caret, selection, and a small amount of nearby text — AT-SPI on Linux, the Accessibility API on macOS. Screenshot context is a separate setting and is off by default. It keeps the image in memory only for the active dictation and sends it only through OpenAI or Anthropic's direct OAuth API path; on Plasma it uses the desktop screenshot portal, on macOS `screencapture` behind the Screen Recording grant. Claude Code session refinement stays text-only.

Refinement styles:

- `Light cleanup`: applies always-on rules plus light cleanup. It stays close to the transcript while fixing punctuation, capitalization, spacing, obvious speech-to-text mistakes, minimal grammar accidents, and explicit corrections.
- `Balanced`: applies always-on, light, and balanced rules. It produces natural dictation that is clean enough to paste anywhere while staying close to what was said; it removes speech artifacts, lightly improves wording, infers simple obvious structure, and handles common corrections.
- `Strong polish`: applies always-on, light, balanced, and strong rules. It rewrites dictated speech into polished, useful text while preserving meaning; it may infer useful organization, consolidate overlap, repair clear insertions or moves, reduce rambling, and handle broad natural corrections.

Output style:

- Adaptive Markdown-compatible output renders normal prose as paragraphs, unordered lists as hyphen bullets, and ordered steps or rankings as numbered lists when that structure is explicit or allowed by the selected refinement style.
- Short simple lists stay inside a sentence when that reads naturally. Standalone ingredients, materials, supplies, items, or options lists prefer a lead-in plus hyphen bullets when the list is the main content or has several items.
- Spoken ordinal cues such as `first step`, `number three`, and `fourth step` are treated as ordered-list structure for procedures, recipes, checklists, rankings, and other obvious sequences.

Refinement level controls how much the transcript may be transformed. Output style controls how permitted structure is rendered. For example, `Light cleanup` still does not infer lists or headings, but it will render explicitly dictated structure clearly. `Balanced` may infer simple obvious lists. `Strong polish` may organize content more aggressively when that makes the result more useful. When rules conflict, Speecher favors always-on preservation rules, explicit user instructions, technical literals, and the least transformative interpretation.

Spoken corrections are applied inside the current capture before delivery. Phrases like `oops remove that`, `scratch that`, `I meant X not Y`, and `replace X with Y` are treated as edits according to the selected refinement style, then removed from the final text.

Technical text is preserved more literally. Commands, paths, URLs, environment variables, identifiers, inline code, config values, issue IDs, and verbatim errors may be wrapped in backticks when clearly dictated. Spoken symbols such as `slash`, `backslash`, `dash`, `underscore`, `dot`, `colon`, `pipe`, `equals`, `plus`, `at`, `hash`, brackets, braces, comma, semicolon, and ampersand are converted to literal characters when the context is technical.

## Credentials

Claude credentials are read from `~/.claude/.credentials.json`. If the token is expired, Speecher tries to refresh it through Claude Code before starting capture.

Anthropic refinement has two auth modes:

- Claude Code session: Speecher starts and keeps an interactive Claude Code `stream-json` session in the background app process, sends refinement turns to it, then sends `/clear` after each result. This uses Claude Code subscription usage and does not use `claude -p`.
- OAuth extra usage: Speecher reads the Claude Code OAuth token from `~/.claude/.credentials.json` and calls the Anthropic Messages API directly with Claude Code OAuth identity headers. Anthropic can route this as usage credits at API rates.

OpenAI refinement defaults to `gpt-6-luna` with effort set to `none`, through the Responses API shape. The Settings picker also includes GPT-6.1 Sol, GPT-6 Astra, GPT-5.6 Luna, and GPT-5.6 Terra. GPT-6.1 Sol has no `none` effort, so pick Low or higher with it.

OpenAI Speed is Standard, Fast (`service_tier=priority`) or Ultrafast (`service_tier=ultrafast`), sent only through a ChatGPT sign-in or CLI Proxy API. Ultrafast uses a lot more usage, needs a plan with Ultrafast access, and so far only serves GPT-6 Astra; other models ask for Fast instead. GPT-6.1 Sol support is coming later. A rejected fast or ultrafast request is retried at standard speed.

Authentication is resolved in this order:

1. If the selected Codex credential store says `auth_mode` is `chatgpt`, use its Codex OAuth token against the ChatGPT Codex backend.
2. The selected store's `OPENAI_API_KEY`, when it starts with `sk-`.
3. The selected store's Codex OAuth token against the ChatGPT Codex backend. Speecher refreshes expired access tokens and reloads that store when it supports writes. macOS Codex Keychain entries require `codex login`.
4. The `OPENAI_API_KEY` environment variable, when it starts with `sk-`.
5. The API key saved in the app settings.

For API-key requests, `OPENAI_ORG_ID` or `OPENAI_ORGANIZATION` is sent as the optional `OpenAI-Organization` header, and `OPENAI_PROJECT_ID` or `OPENAI_PROJECT` is sent as the optional `OpenAI-Project` header. The same values can be provided in the selected Codex credential store alongside `OPENAI_API_KEY`.

The app settings key is stored through QtKeychain when QtKeychain is available at build time. On Linux, QtKeychain uses the desktop keyring backend exposed by the session, such as Secret Service/libsecret-compatible keyrings on GNOME-like desktops or KWallet on KDE; on macOS it uses the system Keychain. If an older plaintext key exists in Qt settings, Speecher attempts to migrate it into the keyring and remove the plaintext setting. If no keyring backend is available or the keyring is locked, saving the app settings key fails instead of silently writing a new plaintext API key.

Claude schema-only diagnostics can be enabled with:

```sh
SPEECHER_DEBUG_CLAUDE_SCHEMA=1 ./build/speecher
```

Speecher mirrors Claude Code's voice stream parameters for Deepgram Nova-3, including typed interim transcripts, and reads the installed Claude Code version at runtime for the voice stream user agent. Typed interims are enabled by default and can be disabled for debugging with:

```sh
SPEECHER_CLAUDE_FORWARD_INTERIMS_TYPED=0 ./build/speecher
```

The diagnostic path records message types and schema keys only.
