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
The language the first speech service in the Service Order listens for, for every Dictation Session, or Automatic where the service detects it. Each later entry of the speech Service Order may have its own; one the user hasn't set takes the main Spoken Language where it listens for it, otherwise English while the main one is Automatic and it takes English, otherwise it is skipped. Distinct from a Writing Profile's output language, which refinement translates into.
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
How refinement treats a Target, chosen by its App or App Type. A profile is built-in (Work, Email, Personal, AI coding, Other) or user-defined, and sets a Cleanup Level, a Tone, Additional Instructions and an optional output language that refinement translates into. It may also name a speech and a refinement Service, with a model, that it tries first, ahead of the page's Service Order; unset, it is the same as the page's. AI coding wins over a terminal's own identity when a coding agent is detected inside it.
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

**Delivery Step**:
One way Speecher gets words out, which the person turns on or off: Insert through accessibility, Press the paste keys (copies the words and presses Ctrl+V, or ⌘V), and Keep the words on the clipboard. Delivery tries Insert, then Press when insertion is off, unavailable or rejected; Keep leaves the words on the clipboard afterwards. With Keep off, Press borrows the clipboard only once Speecher has saved what was there in a form it can put back, and puts it back after a confirmed paste or when no keys went out; a paste it can't confirm, or keys that may have partly gone out, leave the words there. At least one is on.
_Avoid_: Output method, paste mode

**App**:
An application the person set something for, picked from installed apps or apps they dictated into. It holds an App Type, a Writing Profile and a Pasting choice, each "Same as" what its App Type gives until changed. An App is recognized by its ids (desktop file id, bundle id or program name), or, for web apps and programs in a terminal, by text its name or window title contains; a window title never sets the App Type.
_Avoid_: App rule, app match, recognition rule, exception

**App Type**:
The kind of app a Target is: Terminal, Browser, Email, Office, Code editor, AI coding, or Other. Speecher sorts common apps into them from a built-in list, and each carries a Writing Profile and a Pasting choice its apps follow. Other apps' profile is what any app Speecher doesn't recognize writes with.
_Avoid_: Category, app category

**Pasting choice**:
How an App or App Type narrows the Delivery Steps: the steps as they are, Terminal keys, Skip paste keys, or Clipboard only. It never turns on a step that is off. An app's own choice wins, then Terminals' for anything running in a terminal, then its App Type's. The Pasting page lists those that differ from the steps, read-only, and Apps changes them.
_Avoid_: Paste Rule, exception, output route

**Vocabulary Entry**:
One entry of the Vocabulary, Speecher's one list of what it should spell or replace. A spelling is a word or phrase Speecher keeps spelled the person's way: as a key term the speech service takes it as a hint, and refinement reads every spelling, with a context that says what it means and when it applies. A replacement is a spoken phrase and the exact text written in its place, several lines included, where {date} and {time} become today's date and the time. Any entry may be limited to some Writing Profiles and some Apps; anywhere else neither the speech service nor refinement gets it, and no replacement is made. Of two replacements for one phrase, the person's own wins over a Learned Correction, and the narrower one, limited to an App, then to a profile, wins over the wider.
_Avoid_: Word, dictionary word, term (for the entry), binding, snippet (as a kind of its own)

**Learned Correction**:
A replacement Speecher added to the Vocabulary itself, after the person fixed the same words the same way shortly after pasting. It is limited to the app it was learned in until the same fix is seen in another, keeps how many edits showed it and how sure Speecher is, and can be turned off, edited or widened like any other entry.
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
One OAuth login (claude or codex) stored as a JSON file in CLI Proxy API's auth directory: CLI Proxy API is one way to sign in to ChatGPT or to Claude (Sign in with, on the Services page). Claude's sign-in covers Claude Voice dictation and Claude refinement, ChatGPT's covers Codex dictation and ChatGPT refinement. CLI Proxy API owns and refreshes these files; Speecher may also refresh an expired account and writes rotated tokens back while holding the account's adjacent lock file. The directory is auto-detected (`~/.cli-proxy-api`, then `~/.local/share/cliproxy-api/oauth`; override with `cliproxy/oauthDir`). On machines that don't host CLI Proxy API, set its server URL and API key in that service's card on Services instead: refinement then goes through the proxy's own `/v1/messages` and `/v1/responses` endpoints and the server routes accounts itself (speech still needs the local files, since its websockets connect to the vendors directly).
_Avoid_: Proxy token, cliproxy key

**Service**:
Something that turns speech into text or cleans text up: ChatGPT, Claude, This computer (a Local Model for speech, a Local Runner for refinement), or one of Your servers. Each is signed in to or connected once, on the Services page.
_Avoid_: Provider (in the UI), backend, engine

**Service Order**:
The services Speecher tries for one job, speech or refinement, first to last. The first is the main service; the others stand in when it is unavailable. Each entry carries its own model, ChatGPT's and Claude's refinement entries their thinking and speed, and a later speech entry its own Spoken Language. An entry keeps its choices while it is out of the order. Refinement's order runs only while Clean up my words is on.
_Avoid_: Fallback chain, primary and fallbacks, fallbacks

**Clean up my words**:
The switch that turns refinement on or off for every dictation. Off, no refinement service runs, a Writing Profile's own included, and the words are pasted as spoken; the Service Order waits for it to come back on. Distinct from a Writing Profile's Cleanup Level None, which skips cleanup for that profile but still translates into its output language.
_Avoid_: None (as a refinement service)

**Local Model**:
A speech-to-text model file Speecher downloads and runs on the user's own machine through transcribe.cpp, managed on the Local models page under Services.
_Avoid_: Offline model, on-device model, embedded model

**Local Runner**:
A separately installed program that serves cleanup models over HTTP on the user's machine: Ollama, LM Studio or llama-server. Speecher detects it and uses it for refinement but does not run the model itself.
_Avoid_: Local LLM, inference server, backend

**Server**:
One of Your servers: a user-supplied URL that serves one job, one for speech (OpenAI's audio transcriptions API) and one for refinement (OpenAI Chat Completions or Anthropic Messages). Its model is its entry's in that job's Service Order. The refinement server can be pointed at the CLI Proxy API server, and then sends that server's key.
_Avoid_: Custom Endpoint (retired), BYO endpoint, self-hosted provider

**Hardware Tier**:
The class Speecher assigns a machine from its detected GPU memory, RAM and CPU, used to label each Local Model fits, tight or too large and to pick a suggestion.
_Avoid_: Device class, performance level

**Speed Test**:
A short bundled clip transcribed with a downloaded Local Model on the user's machine; its measured time replaces the estimated speed label.
_Avoid_: Benchmark, calibration
