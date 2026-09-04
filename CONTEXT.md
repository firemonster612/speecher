# Speecher

Speecher turns a short spoken input into text for a chosen desktop target. It can clean that text, place it on the clipboard, and attempt to insert it into the target.

## Language

**Dictation Session**:
One recording that starts through toggle or push-to-talk and ends after Speecher produces and delivers text.
_Avoid_: Recording job, transcription run

**Raw Transcript**:
The speech provider's final text before optional cleanup.
_Avoid_: Unformatted result

**Refined Transcript**:
The final text after optional language-model cleanup.
_Avoid_: AI transcript, formatted transcript

**Target**:
The desktop application and editable control selected when a Dictation Session starts.
_Avoid_: Destination, focused app

**Writing Profile**:
The Work, Email, Personal, or Other category inferred from the Target, with a user-selected fallback and optional override.
_Avoid_: Style preset, persona

**Paste Rule**:
A setting that chooses how Speecher attempts insertion for an application or application category.
_Avoid_: Output route, injection rule

**Vocabulary Entry**:
A word or phrase Speecher should recognize, preserve, or replace during dictation.
_Avoid_: Dictionary word

**Snippet**:
A short spoken trigger that expands to longer user-written text.
_Avoid_: Macro, template

**Learned Correction**:
A local Vocabulary Entry inferred from a user's edit shortly after insertion.
_Avoid_: Training sample, correction history

**Global Shortcut**:
The system-wide key combination that toggles a Dictation Session from anywhere on the desktop.
_Avoid_: Hotkey, global hotkey, keybinding

**Update Channel**:
The stream of releases an installed Speecher follows: Stable or Nightly. A per-user setting, defaulting to Stable.
_Avoid_: Track, branch, ring

**Stable Release**:
A hand-tested version published deliberately for general use.
_Avoid_: Official release, production build

**Nightly Build**:
The untested prerelease republished automatically from every push to master. Despite the name, it follows pushes, not the calendar.
_Avoid_: Dev build, edge, snapshot

**What's New Page**:
The settings page shown after an upgrade, containing the release notes and real settings introduced since the previous run.
_Avoid_: Changelog page, update summary

**Last-run Version**:
The Speecher version recorded the last time the application started, used as the beginning of the next What's New range.
_Avoid_: Previous release, installed version

**Since Version**:
The first Stable Release in which a setting appears, used to include that setting on the What's New Page after an upgrade.
_Avoid_: Added version, introduced version

**CLI Proxy API Account**:
One OAuth login (claude or codex) stored as a JSON file in CLI Proxy API's auth directory and selectable as an account source on Accounts. The Anthropic mode covers Claude Voice dictation and Anthropic refinement. The OpenAI mode covers Codex dictation and OpenAI refinement. CLI Proxy API owns and refreshes these files; Speecher may also refresh an expired account and writes rotated tokens back while holding the account's adjacent lock file. The directory is auto-detected (`~/.cli-proxy-api`, then `~/.local/share/cliproxy-api/oauth`; override with `cliproxy/oauthDir`). On machines that don't host CLI Proxy API, set its server URL and API key on Accounts. Refinement requests then use the proxy's `/v1/messages` and `/v1/responses` endpoints, and the server picks the account. Speech still needs the local files because its WebSocket connections go straight to the vendors.
_Avoid_: Proxy token, cliproxy key
