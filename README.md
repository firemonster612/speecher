<p align="center">
  <img src="packaging/io.github.firemonster612.speecher.svg" alt="Speecher icon" width="96" height="96">
</p>

<h1 align="center">speecher</h1>

<p align="center">Speech-to-text for Linux and macOS that reuses your existing subscriptions.</p>

## Quick start

### Prerequisites

Sign in to at least one transcription service: Claude Code for Claude Voice, or the ChatGPT app or Codex CLI for ChatGPT Codex dictation. Speecher can refresh expired logins through the matching CLI. It looks for `claude` and `codex` on `PATH`, common locations such as `~/.local/bin`, and the CLI bundled with the Linux ChatGPT app.

```sh
# Arch
sudo pacman -S cmake ninja gcc qt6-base qt6-multimedia qt6-websockets qt6-wayland layer-shell-qt qtkeychain-qt6 wl-clipboard at-spi2-core

# Debian
sudo apt install cmake ninja-build g++ qt6-base-dev qt6-multimedia-dev qt6-websockets-dev qt6-wayland liblayershellqtinterface-dev qtkeychain-qt6-dev wl-clipboard libatspi2.0-dev

# Fedora
sudo dnf install cmake ninja-build gcc-c++ qt6-qtbase-devel qt6-qtmultimedia-devel qt6-qtwebsockets-devel qt6-qtwayland layer-shell-qt-devel qtkeychain-qt6-devel wl-clipboard at-spi2-core-devel

# macOS
brew install cmake ninja pkgconf qt qtkeychain
```

### Install

```sh
make install
```

The Makefile detects your platform; pass `PLATFORM=linux` or `PLATFORM=macos` to be explicit.

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

The app is unsigned because there is no Apple Developer account. Later updates are verified with Sparkle EdDSA signatures.

The default Update Channel is Stable Release. Nightly Builds are republished from every push to `master`, not on a nightly schedule. Switch channels in **Settings > General > Updates**.

### Global shortcut

On macOS, Speecher registers its own global hotkey — set it in the setup assistant or Settings, including press-and-hold push-to-talk. Nothing to configure outside the app.

On Linux the compositor owns global shortcuts, so bind a key to the CLI. On KDE Plasma:

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

1. Open `System Settings > Keyboard > Shortcuts`.
2. Select `Add New > Command or Script`.
3. Set the command to `/path/to/speecher toggle`.
4. Click `Add`.
5. Assign your preferred shortcut under `Custom Shortcuts`.

Speecher also has separate `start` and `stop` commands for press-and-hold setups. Bind the key-press action to `speecher start` and the matching key-release action to `speecher stop` in whichever Plasma shortcut tool or input remapper you use.

Add `--format html` or `--format plain` to `toggle` or `start` when you want a shortcut that overrides the saved output format for one dictation.

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

Required Qt modules: Core, Widgets, Network, Multimedia, and Qt WebSockets. On Linux, AT-SPI development files are used for target discovery, context, and paste verification; on macOS the same jobs go through the Accessibility API and need no extra packages.

## AppImage

Build a portable AppImage with:

```sh
packaging/build-appimage.sh
```

The script creates `dist/Speecher-x86_64.AppImage`. It uses CMake install output and `appimagetool`. If `wl-copy` is installed on the build machine, it is bundled into the AppImage by default; pass `--no-bundle-wl-clipboard` to keep wl-clipboard external.

## DMG

```sh
make dmg
```

builds `build/speecher.dmg`: a copy of the app run through `macdeployqt` so Qt travels inside the bundle, plus an `/Applications` symlink in the classic drag-install layout. The script is `packaging/macos/build-dmg.sh`.

## Run

```sh
./build/speecher            # Linux
open build/speecher.app     # macOS
./build/speecher toggle
./build/speecher start
./build/speecher stop
./build/speecher status
./build/speecher --version
```

The four CLI commands contact the running app through a per-user socket (on macOS the binary lives at `build/speecher.app/Contents/MacOS/speecher`). `toggle` switches recording on or off, `start` only starts it, `stop` only stops it, and `status` prints the current state. If `toggle` or `start` can't find a running instance, it starts a popup-only background process and begins listening. Calling `stop` or `status` without a running instance prints `idle`.

On Linux, Speecher uses one window with a KDE-style sidebar, searchable settings pages, and dictation controls; `speecher settings` opens it on General settings. On macOS, Speecher is a menu bar app: dictation lives in the menu bar item and a floating panel, and settings open in a native window from the menu bar, the Dock, or ⌘,.

## Transcription

Choose Claude Voice or ChatGPT Codex under the transcription settings. The setup assistant reads the same provider registry, so newly registered transcription services appear in both places without separate wizard changes.

ChatGPT Codex dictation uses the same streaming protocol as the Codex CLI and reads its ChatGPT OAuth session from `~/.codex/auth.json`. An OpenAI API key cannot authorize this endpoint. On Linux, the desktop package launcher is `/usr/bin/chatgpt` and its bundled CLI is `/usr/lib/chatgpt/resources/codex`, which Speecher can use to refresh an expired login. A standalone Codex CLI on `PATH` works everywhere.

Native binaries use one stable user socket, so the desktop app and CLI shortcut talk to the same instance after `make install`. AppImages have their own stable socket because their internal mounted path changes on each launch.

## Refinement

OpenAI and Anthropic refinement can be tuned in Settings. On first run, Speecher defaults refinement to OpenAI when the Codex CLI is installed; if Codex is not installed but Claude Code is installed, it defaults to Anthropic. Explicit provider choices in Settings are preserved. The default style is `Balanced` with adaptive Markdown-compatible output. Refinement is built from composable rules: always-on preservation rules, cumulative level rules, output-style rules, and conflict-resolution rules.

Anthropic refinement defaults to Claude Sonnet 4.6. The Settings model picker shows one simple display name per built-in Claude model, such as Claude Opus 4.8, Claude Sonnet 4.6, and Claude Haiku 4.5. It intentionally excludes Fable 5 and Mythos 5 after their June 13, 2026 access suspension. The field is editable for newer or account-specific model IDs. Speecher warns when a Haiku model is selected because Haiku has been observed interpreting the transcript being refined as instructions.

Refinement effort is configurable per provider. OpenAI effort maps to `reasoning.effort` on the Responses API and defaults to `none` with GPT-5.6 Luna. Anthropic effort maps to Claude Code's interactive `--effort` flag in Claude Code session mode, and to Anthropic adaptive thinking plus `output_config.effort` in OAuth extra usage mode when the selected model supports it. Anthropic effort defaults to `low`.

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

OpenAI refinement defaults to `gpt-5.6-luna` with effort set to `none`, through the Responses API shape. The Settings picker also includes GPT-5.6 Terra and GPT-5.6 Sol.

Authentication is resolved in this order:

1. If `~/.codex/auth.json` says `auth_mode` is `chatgpt`, use its Codex OAuth token against the ChatGPT Codex backend.
2. `~/.codex/auth.json` `OPENAI_API_KEY`, when it starts with `sk-`.
3. `~/.codex/auth.json` Codex OAuth token against the ChatGPT Codex backend. If the OAuth access token is expired, Speecher asks a standalone or ChatGPT-bundled Codex CLI to refresh it and reloads the auth file.
4. The `OPENAI_API_KEY` environment variable, when it starts with `sk-`.
5. The API key saved in the app settings.

For API-key requests, `OPENAI_ORG_ID` or `OPENAI_ORGANIZATION` is sent as the optional `OpenAI-Organization` header, and `OPENAI_PROJECT_ID` or `OPENAI_PROJECT` is sent as the optional `OpenAI-Project` header. The same values can be provided in `~/.codex/auth.json` alongside `OPENAI_API_KEY`.

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
