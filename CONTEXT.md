# Speecher

Speecher turns a short spoken input into text for a chosen desktop target. It can clean that text, place it on the clipboard, and attempt to insert it into the target.

## Language

**Dictation Session**:
One dictation, from the microphone opening through toggle or push-to-talk until Speecher produces and delivers text. Not a Recording, which is a call written to a file.
_Avoid_: Recording job, transcription run

**Recording**:
A call recorded into a transcript file, started and stopped with `speecher record`. It streams the microphone (`me`) and, where Speecher captures it, system audio (`them`) to a speech provider, each in its own session, and appends a timed line per utterance as it runs, beside any Dictation Session, and nothing is pasted, copied or refined. Distinct from a Dictation Session, which ends by delivering one text.
_Avoid_: Call session, meeting mode

**Spoken Language**:
The language the speech service listens for, one setting for every Dictation Session, or Automatic where the service detects it. Distinct from a Writing Profile's output language, which refinement translates into.
_Avoid_: Dictation language, input language, locale

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
How refinement treats a Target, inferred from it, with a user-selected fallback and optional override. A profile is built-in (Work, Email, Personal, AI coding, Other) or user-defined, and sets a Cleanup Level, a Tone, Additional Instructions and an optional output language that refinement translates into. AI coding wins over a terminal's own identity when a coding agent is detected inside it.
_Avoid_: Style preset, persona

**Cleanup Level**:
How much refinement may rewrite the Raw Transcript: None, Light, Medium, High, or a user-defined level built on one of them.
_Avoid_: Refinement style, polish level

**Tone**:
The voice a Writing Profile asks refinement to write in, built-in or user-defined, or no tone override.
_Avoid_: Style, voice preset

**Additional Instructions**:
The user's own text added to the refinement prompt: once for every refinement, then once per Writing Profile.
_Avoid_: Custom prompt, system prompt

**Paste Rule**:
A setting that chooses how Speecher attempts insertion for an application or application category.
_Avoid_: Output route, injection rule

**Vocabulary Entry**:
A word or phrase Speecher should recognize, preserve, or replace during dictation. It may carry a context, which tells refinement what it means and when it applies, and may be limited to some Writing Profiles; under any other profile neither the speech service nor refinement gets it.
_Avoid_: Dictionary word

**Snippet**:
A short spoken trigger that expands to longer user-written text.
_Avoid_: Macro, template

**Learned Correction**:
A local Vocabulary Entry inferred from a user's edit shortly after insertion.
_Avoid_: Training sample, correction history

**Global Shortcut**:
The system-wide key combination that toggles a Dictation Session from anywhere on the desktop. While a selection edit waits for review, it dictates a follow-up instruction that changes the edit instead.
_Avoid_: Hotkey, global hotkey, keybinding

**Cancel Shortcut**:
An optional Global Shortcut that cancels the Dictation Session in progress: nothing is pasted, copied or recorded, and the tray keeps the words heard so far to copy. Unbound until the user sets one. It acts only during a session, so where the desktop lets Speecher take a key just for the session (Windows, macOS, Plasma, X11) it may be a bare key such as C or Escape. On Windows and macOS, Escape also cancels while a session is active, unless Escape is the Cancel or Pause Shortcut. While a selection edit waits for review, the Cancel Shortcut and Escape keep the original selection and Enter replaces it; Plasma and X11 take Escape and Enter for the review as well. While a follow-up instruction is dictated, they drop the follow-up and keep the review.
_Avoid_: Abort key, escape shortcut

**Pause Shortcut**:
An optional Global Shortcut that pauses the Dictation Session in progress and, pressed again, resumes it. Unbound until the user sets one, and like the Cancel Shortcut it acts only during a session and may be a bare key where the desktop allows. The two together are the session shortcuts.
_Avoid_: Hold key, mute shortcut

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
One OAuth login (claude or codex) stored as a JSON file in CLI Proxy API's auth directory, selectable as a credential source per provider family (Accounts page): the Anthropic auth mode covers Claude Voice dictation and Anthropic refinement, the OpenAI auth mode covers Codex dictation and OpenAI refinement. CLI Proxy API owns and refreshes these files; Speecher may also refresh an expired account and writes rotated tokens back while holding the account's adjacent lock file. The directory is auto-detected (`~/.cli-proxy-api`, then `~/.local/share/cliproxy-api/oauth`; override with `cliproxy/oauthDir`). On machines that don't host CLI Proxy API, set the server URL and API key on the Accounts page instead: refinement then goes through the proxy's own `/v1/messages` and `/v1/responses` endpoints and the server routes accounts itself (speech still needs the local files, since its websockets connect to the vendors directly).
_Avoid_: Proxy token, cliproxy key

**Local Model**:
A speech-to-text model file Speecher downloads and runs on the user's own machine through transcribe.cpp, managed on the Local models page.
_Avoid_: Offline model, on-device model, embedded model

**Local Runner**:
A separately installed program that serves cleanup models over HTTP on the user's machine: Ollama, LM Studio or llama-server. Speecher detects it and uses it for refinement but does not run the model itself.
_Avoid_: Local LLM, inference server, backend

**Custom Endpoint**:
A user-supplied server URL for one role, speech or refinement. The speech endpoint speaks OpenAI's audio transcriptions API; the refinement endpoint speaks OpenAI Chat Completions or Anthropic Messages. The CLI Proxy API server is a preset of the refinement endpoint.
_Avoid_: BYO endpoint, self-hosted provider, custom provider

**Hardware Tier**:
The class Speecher assigns a machine from its detected GPU memory, RAM and CPU, used to label each Local Model fits, tight or too large and to pick a suggestion.
_Avoid_: Device class, performance level

**Speed Test**:
A short bundled clip transcribed with a downloaded Local Model on the user's machine; its measured time replaces the estimated speed label.
_Avoid_: Benchmark, calibration
