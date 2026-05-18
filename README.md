<p align="center">
  <img src="packaging/local.speecher.svg" alt="Speecher icon" width="96" height="96">
</p>

<h1 align="center">speecher</h1>

<p align="center"> Linux Speech-To-Text app that reuses your existing subscriptions.</p>

## Quick start

### Prerequisites
Be signed into Claude Code CLI (required) and Codex CLI (optionaly) if you want refinement. Speecher can refresh expired Claude logins by running `claude auth status`, so `claude` should be available on `PATH`; it also checks common Claude Code install locations such as `~/.local/bin/claude`.

```sh
# Arch
sudo pacman -S cmake ninja gcc qt6-base qt6-multimedia qt6-websockets qt6-wayland layer-shell-qt qtkeychain-qt6 wtype wl-clipboard

# Debian
sudo apt install cmake ninja-build g++ qt6-base-dev qt6-multimedia-dev qt6-websockets-dev qt6-wayland liblayershellqtinterface-dev qtkeychain-qt6-dev wtype wl-clipboard

# Fedora
sudo dnf install cmake ninja-build gcc-c++ qt6-qtbase-devel qt6-qtmultimedia-devel qt6-qtwebsockets-devel qt6-qtwayland layer-shell-qt-devel qtkeychain-qt6-devel wtype wl-clipboard
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

The script creates `dist/Speecher-x86_64.AppImage`. It uses CMake install output and `appimagetool`. If `wtype` and `wl-copy` are installed on the build machine, they are bundled into the AppImage by default; pass `--no-bundle-wtype` to keep wtype external and `--no-bundle-wl-clipboard` to keep wl-clipboard external.

## Run

```sh
./build/speecher
./build/speecher toggle
./build/speecher --version
```

`speecher toggle` contacts the running app through a per-user socket. Native binaries scope the socket to the executable path, so `./build/speecher`, `~/.local/bin/speecher`, and `/usr/local/bin/speecher` do not accidentally control each other. AppImages use one stable AppImage socket, because the internal mounted executable path changes on every launch. If nothing is running for that socket, the command starts a popup-only background process and begins listening.

## Refinement

OpenAI refinement can be tuned in Settings. The default is `Balanced` with `Plain sentences` output.

Refinement styles:

- `Strong polish`: aggressively rewrites dictation into polished text, removes speech artifacts, infers structure, and handles broad natural corrections.
- `Balanced`: fixes transcription issues, lightly improves wording, infers clear structure cues, and handles common corrections.
- `Light cleanup`: stays close to the transcript while fixing punctuation, capitalization, spacing, and explicit corrections.

Output formats:

- `Plain sentences`: prefers compact paragraphs and sentence-style lists. Explicit paragraph and line-break cues are still honored.
- `Markdown style`: may use headings, hyphen bullets, numbered lists, and blank lines when useful.

Spoken corrections are applied inside the current capture before delivery. Phrases like `oops remove that`, `scratch that`, `I meant X not Y`, and `replace X with Y` are treated as edits according to the selected refinement style, then removed from the final text.

Technical text is preserved more literally. Commands, paths, URLs, environment variables, inline code, and verbatim errors may be wrapped in backticks when clearly dictated, and spoken symbols such as `slash`, `dash`, `underscore`, `dot`, `colon`, `pipe`, and `equals` are converted to literal characters when the context is technical.

## Credentials

Claude credentials are read from `~/.claude/.credentials.json`. If the token is expired, Speecher tries to refresh it through Claude Code before starting capture.

OpenAI refinement uses `gpt-5.4-mini` through the Responses API shape.

Authentication is resolved in this order:

1. If `~/.codex/auth.json` says `auth_mode` is `chatgpt`, use its Codex OAuth token against the ChatGPT Codex backend.
2. `~/.codex/auth.json` `OPENAI_API_KEY`, when it starts with `sk-`.
3. `~/.codex/auth.json` Codex OAuth token against the ChatGPT Codex backend.
4. The `OPENAI_API_KEY` environment variable, when it starts with `sk-`.
5. The API key saved in the app settings.

For API-key requests, `OPENAI_ORG_ID` or `OPENAI_ORGANIZATION` is sent as the optional `OpenAI-Organization` header, and `OPENAI_PROJECT_ID` or `OPENAI_PROJECT` is sent as the optional `OpenAI-Project` header. The same values can be provided in `~/.codex/auth.json` alongside `OPENAI_API_KEY`.

The app settings key is stored through QtKeychain when QtKeychain is available at build time. On Linux, QtKeychain uses the desktop keyring backend exposed by the session, such as Secret Service/libsecret-compatible keyrings on GNOME-like desktops or KWallet on KDE. If an older plaintext key exists in Qt settings, Speecher attempts to migrate it into the keyring and remove the plaintext setting. If no keyring backend is available or the keyring is locked, saving the app settings key fails instead of silently writing a new plaintext API key.

Claude schema-only diagnostics can be enabled with:

```sh
SPEECHER_DEBUG_CLAUDE_SCHEMA=1 ./build/speecher
```

The diagnostic path records message types and schema keys only.
