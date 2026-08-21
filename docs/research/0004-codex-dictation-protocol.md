# Codex dictation stream contract (verified live 2026-08-21)

Endpoint: `wss://chatgpt.com/backend-api/dictation/stream`, subprotocols `chatgpt-dictation` + `openai-bearer.<codex oauth access token>`, Chromium desktop User-Agent required (Cloudflare browser gate).

## Session config (`session.start` → `config`)

`transcript_delivery_mode` accepts exactly `final_only`, `segment`, or `delta` (server error enumerates them; probed live). `delta` is normalized to `segment` in the echoed config; both behave identically as of 2026-08-21.

- `final_only`: **no transcript events while streaming**. A single `transcript.final` per utterance arrives only after `audio.flush`. This is why the live preview went dark while the client requested this mode.
- `segment`: server streams `transcript.segment` events (~2-4/s while speaking): `{type, sequence_no, utterance_id, revision, text}`, where `text` is the cumulative transcript of the current utterance (may carry a leading space) and `revision` increments per update. The authoritative `transcript.final` for the utterance still arrives at flush/endpoint, same shape.

## Other observed events

- `session.started` / `session.updated` (one `session.updated` per ~audio append; carries buffered byte counts; `status: "closed"` after `session.close`).
- `speech.started` / `speech.stopped` with `utterance_id` (server VAD). Note: digital-zero silence (1.5 s) did NOT trigger VAD endpointing mid-session in probes; `speech.stopped` fired only on `audio.flush`.
- `transcript.failed`, `session.error` (`fatal` flag; `error.code/message/retryable`).

## Probe harness

`.scratch/codex-preview/probe.ts` (bun) replays `.scratch/codex-preview/speech.pcm` (piper-synthesized 16 kHz mono s16le) against the live endpoint with configurable modes and prints a per-event timeline plus a GREEN/RED verdict on live transcripts. The opt-in suite test `CodexDictationTests::liveCodexDictationProvider` (env `SPEECHER_TEST_LIVE_CODEX_PCM=<pcm>`) runs the same check through the real transcriber and asserts partials arrive.
