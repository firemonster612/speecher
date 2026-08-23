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

**CLI Proxy API Account**:
One OAuth login (claude or codex) stored as a JSON file in CLI Proxy API's auth directory, selectable as a credential source for refinement and for speech transcription (Audio page → Speech credentials). CLI Proxy API owns and refreshes these files; Speecher only reads them. The directory is auto-detected (`~/.cli-proxy-api`, then `~/.local/share/cliproxy-api/oauth`; override with `cliproxy/oauthDir`). On machines that don't host CLI Proxy API, set the server URL and API key on the Providers page instead: refinement then goes through the proxy's own `/v1/messages` and `/v1/responses` endpoints and the server routes accounts itself (speech still needs the local files, since its websockets connect to the vendors directly).
_Avoid_: Proxy token, cliproxy key
