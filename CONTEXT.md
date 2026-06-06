# Speecher Context

## Domain Vocabulary

- **Dictation session**: One user-initiated capture cycle from start through speech recognition, optional refinement, final text delivery, and return to idle.
- **Speech transcriber**: A provider adapter that accepts PCM audio chunks and emits partial/final transcript text. Claude Voice is the current adapter.
- **Transcript refiner**: A provider adapter that mutates raw dictated text into cleaner output. OpenAI is the current adapter; `none` disables refinement.
- **Provider registry**: The in-process registry that maps configured provider IDs to speech transcriber and transcript refiner adapters.
- **Platform integration**: The module that chooses OS-specific adapters for audio, media pause/resume, popup placement, text delivery, IPC naming, and detached startup.
- **Text delivery adapter**: A platform-specific adapter that inserts or copies final text. Linux currently uses `wtype`, then `wl-copy`, then Qt clipboard fallback.
- **Settings snapshot**: A typed read of persisted settings used by workflow code. Existing `QSettings` keys remain the compatibility surface.
- **Transcriber popup**: The small status surface shown during a dictation session. It presents workflow signals but does not own workflow behavior.

## Module Intent

- Keep dictation workflow behavior local to `DictationSession`.
- Keep provider protocol and credential details inside provider adapters.
- Keep Linux details behind `PlatformIntegration` and its adapters.
- Keep Qt Widgets as the desktop shell unless a future ADR changes the application stack.
