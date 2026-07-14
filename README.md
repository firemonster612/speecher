<p align="center">
  <img src="packaging/local.speecher.svg" alt="Speecher icon" width="96" height="96">
</p>

<h1 align="center">speecher</h1>

<p align="center"> Linux Speech-To-Text app that reuses your existing subscriptions.</p>

## Quick start

### Prerequisites
Be signed into Claude Code CLI (required) and Codex CLI (optionally) if you want refinement through Codex OAuth. Speecher can refresh expired Claude logins through Claude Code, and can ask Codex CLI to refresh an expired Codex OAuth token, so `claude` and `codex` should be available on `PATH`; it also checks common install locations such as `~/.local/bin`.

```sh
# Arch
sudo pacman -S cmake ninja gcc qt6-base qt6-multimedia qt6-websockets qt6-wayland layer-shell-qt qtkeychain-qt6 wl-clipboard

# Debian
sudo apt install cmake ninja-build g++ qt6-base-dev qt6-multimedia-dev qt6-websockets-dev qt6-wayland liblayershellqtinterface-dev qtkeychain-qt6-dev wl-clipboard

# Fedora
sudo dnf install cmake ninja-build gcc-c++ qt6-qtbase-devel qt6-qtmultimedia-devel qt6-qtwebsockets-devel qt6-qtwayland layer-shell-qt-devel qtkeychain-qt6-devel wl-clipboard
```

### Install

```sh
make install
```

By default this installs to your per-user prefix, `~/.local`:

- binary: `~/.local/bin/speecher`
- desktop file: `~/.local/share/applications/local.speecher.desktop`
- icon: `~/.local/share/icons/hicolor/scalable/apps/local.speecher.svg`

To install somewhere else:

```sh
make install PREFIX=/usr/local
```

or

Run: `./packaging/build-appimage.sh`

Resulting appimage: `./dist/Speecher-x86_64.AppImage`

Install the appimage however you like

### Keybind (optional)

Use a custom keyboard shortcut to run:

```sh
/path/to/speecher toggle
```

If you installed with the default `make install`, the command is:

```sh
~/.local/bin/speecher toggle
```

If you installed and Appimage:

```sh
/path/to/Speecher-x86_64.AppImage toggle
```

#### KDE Plasma

1. Open `System Settings > Keyboard > Shortcuts`.
2. Select `Add New > Command or Script`.
3. Set the command to `/path/to/speecher toggle`.
4. Click `Add`.
5. Assign your preferred shortcut under `Custom Shortcuts`.

#### GNOME

1. Open `Settings`.
2. Go to `Keyboard`.
3. In `Keyboard Shortcuts`, select `View and Customize Shortcuts`.
4. Select `Custom Shortcuts`.
5. Click `Add Shortcut` or the `+` button.
6. Set `Name` to `Speecher toggle`.
7. Set `Command` to `/path/to/speecher toggle`.
8. Click `Add Shortcut`, press the shortcut you want to use, then click `Add`.

#### Cinnamon

1. Open the Cinnamon menu and search for `Keyboard`, or open `System Settings > Keyboard`.
2. Select the `Shortcuts` tab.
3. Select `Custom Shortcuts`.
4. Click `Add custom shortcut`.
5. Set the name to `Speecher toggle`.
6. Set the command to `/path/to/speecher toggle`.
7. Add the shortcut, then double-click the unassigned keyboard binding row and press the shortcut you want to use.

## Build

```sh
make
```
or

```sh
cmake -S . -B build -G Ninja -DSPEECHER_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Required Qt modules: Core, Widgets, Network, Multimedia, Qt WebSockets development files

## AppImage

Build a portable AppImage with:

```sh
packaging/build-appimage.sh
```

The script creates `dist/Speecher-x86_64.AppImage`. It uses CMake install output and `appimagetool`. If `wl-copy` is installed on the build machine, it is bundled into the AppImage by default; pass `--no-bundle-wl-clipboard` to keep wl-clipboard external.

## Run

```sh
./build/speecher
./build/speecher toggle
./build/speecher --version
```

`speecher toggle` contacts the running app through a per-user socket. Native binaries use one stable user socket so the desktop app and CLI keybind talk to the same instance after `make install`; the CLI also checks the older executable-path socket for compatibility with already-running older builds. AppImages use one stable AppImage socket, because the internal mounted executable path changes on every launch. If no compatible instance is running, the command starts a popup-only background process and begins listening.

## Refinement

OpenAI and Anthropic refinement can be tuned in Settings. On first run, Speecher defaults refinement to OpenAI when the Codex CLI is installed; if Codex is not installed but Claude Code is installed, it defaults to Anthropic. Explicit provider choices in Settings are preserved. The default style is `Balanced` with adaptive Markdown-compatible output. Refinement is built from composable rules: always-on preservation rules, cumulative level rules, output-style rules, and conflict-resolution rules.

Anthropic refinement defaults to Claude Sonnet 4.6. The Settings model picker shows one simple display name per built-in Claude model, such as Claude Opus 4.8, Claude Sonnet 4.6, and Claude Haiku 4.5. It intentionally excludes Fable 5 and Mythos 5 after their June 13, 2026 access suspension. The field is editable for newer or account-specific model IDs. Speecher warns when a Haiku model is selected because Haiku has been observed interpreting the transcript being refined as instructions.

Refinement effort is configurable per provider. OpenAI effort maps to `reasoning.effort` on the Responses API and defaults to `none` with GPT-5.6 Luna. Anthropic effort maps to Claude Code's interactive `--effort` flag in Claude Code session mode, and to Anthropic adaptive thinking plus `output_config.effort` in OAuth extra usage mode when the selected model supports it. Anthropic effort defaults to `low`.

Settings also includes output controls for choosing how Speecher delivers text, including setup for typing directly into focused text fields. When virtual-keyboard paste is enabled, Speecher can optionally restore the previous clipboard contents after delivery.

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
3. `~/.codex/auth.json` Codex OAuth token against the ChatGPT Codex backend. If the OAuth access token is expired, Speecher asks the Codex CLI to refresh it and reloads the auth file.
4. The `OPENAI_API_KEY` environment variable, when it starts with `sk-`.
5. The API key saved in the app settings.

For API-key requests, `OPENAI_ORG_ID` or `OPENAI_ORGANIZATION` is sent as the optional `OpenAI-Organization` header, and `OPENAI_PROJECT_ID` or `OPENAI_PROJECT` is sent as the optional `OpenAI-Project` header. The same values can be provided in `~/.codex/auth.json` alongside `OPENAI_API_KEY`.

The app settings key is stored through QtKeychain when QtKeychain is available at build time. On Linux, QtKeychain uses the desktop keyring backend exposed by the session, such as Secret Service/libsecret-compatible keyrings on GNOME-like desktops or KWallet on KDE. If an older plaintext key exists in Qt settings, Speecher attempts to migrate it into the keyring and remove the plaintext setting. If no keyring backend is available or the keyring is locked, saving the app settings key fails instead of silently writing a new plaintext API key.

Claude schema-only diagnostics can be enabled with:

```sh
SPEECHER_DEBUG_CLAUDE_SCHEMA=1 ./build/speecher
```

Speecher mirrors Claude Code's voice stream parameters for Deepgram Nova-3, including typed interim transcripts, and reads the installed Claude Code version at runtime for the voice stream user agent. Typed interims are enabled by default and can be disabled for debugging with:

```sh
SPEECHER_CLAUDE_FORWARD_INTERIMS_TYPED=0 ./build/speecher
```

The diagnostic path records message types and schema keys only.
