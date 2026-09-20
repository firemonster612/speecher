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

## Free-form context / prompt biasing (probed live 2026-09-20)

The `session.start` config schema is strict (Pydantic; unknown keys fail with
`invalid_event` / "Extra inputs are not permitted"), so absence is provable, not
just unobserved. There is **no free-form context or biasing field anywhere in the
protocol**:

- Rejected as unknown config keys: `prompt`, `transcription_prompt`,
  `initial_prompt`, `biasing`, `bias`, `glossary`, `vocabulary`,
  `custom_vocabulary`, `keyterms`, `keyterm`, `hints`, `hint`, `instructions`,
  `text`, `model`, `temperature`, `provider`, `context`, `noise_reduction`.
  Top-level `prompt`/`context` beside `config` are rejected the same way.
- The complete client event set (server-enumerated via a bogus `type`):
  `session.start`, `session.update`, `session.close_intent`, `audio.append`,
  `audio.flush`, `audio.clear`, `session.close`. No context-carrying event.
- `session.update` takes a `config` subset (e.g. `transcript_delivery_mode`
  works); `language` and unknown keys are rejected there.

Undocumented but real config keys found while probing: `language` (validated,
"Invalid transcription language" for bad values; `"en"` accepted at
`session.start` only), `session_asset_mode` (`none`/`session_wav`),
`session_asset_delivery_mode` (`reserved`/`committed`),
`provider_close_wait_timeout_ms` (int). The echoed config also reveals these
defaults: `session_asset_mode: "session_wav"`, `session_asset_delivery_mode:
"reserved"`, `provider_close_wait_timeout_ms: 55000`.

Conclusion: transcript biasing has to happen client-side (the vocabulary-aware
refinement stage). The public Realtime API's `prompt` field for
`gpt-4o-transcribe` has no counterpart on this endpoint.

## Final-pass quality and two-pass options (probed live 2026-09-20)

- In `segment` mode, `transcript.final` is byte-identical to the last
  `transcript.segment` (both misheard the same test words). `provider_mode:
  "buffered"` with `final_only` produced the same text again. One model, no
  higher-quality server-side second pass on this socket, and no knob that
  trades latency for accuracy.
- `session_asset_delivery_mode: "committed"` yields `asset.ready` after flush
  with a `sediment://file_...` pointer to the whole-session WAV, `asset_ttl`
  30d, so the server retains the audio; we did not probe how to download it.
  `session.close_intent` requires an additional field we did not identify
  ("Field required").
- **`POST https://chatgpt.com/backend-api/transcribe`** (multipart `file`,
  WAV) works with the same Codex OAuth bearer + Chromium UA and returns
  `{"text": ...}`. It is a genuinely different decode from the dictation
  socket (it fixed a tense error the streaming pass made on the same audio).
  Latency: single POST after the audio exists, so it fits a
  "live preview via socket, authoritative retranscribe at flush" design.
- The batch endpoint truncates long recordings: a 292 s WAV came back with only
  the first ~86 s worth of text (1322 of ~4500 chars), returned as a normal 200
  success. Any consumer replacing a streamed transcript with the batch text
  must length-check it first. Latency scales with recording length but stays
  moderate (measured stop→result: 3 s audio ≈ +0.8 s, 73 s ≈ +3.9 s, 292 s
  (9.3 MB upload) ≈ +4.0 s over the streaming baseline of ~0.7-0.9 s).
- That batch endpoint ignores extra form fields rather than rejecting them:
  `prompt`, `context`, `language`, and a deliberately bogus field all return
  200 with output identical to baseline across repeated runs (decoding is
  deterministic for a given file). So no free-form context there either.
- `https://chatgpt.com/backend-api/audio/transcriptions` and
  `/backend-api/dictation/transcribe` are 404. `api.openai.com/v1/audio/
  transcriptions` rejects the Codex bearer path with a billing error
  (metered API, not covered by the ChatGPT subscription).

## Probe harness

`.scratch/codex-context-probe/{probe,sweep,update,update2,full,batch,batch2,batch3}.ts`
(bun; `full.ts` streams `speech.pcm`, espeak-synthesized 16 kHz mono s16le;
`batch*.ts` hit the backend transcribe endpoint) cover the probes above: each opens a live socket, sends one `session.start` /
`session.update` variant, and prints the server's verdict. The original
`.scratch/codex-preview/probe.ts` (bun) replays `.scratch/codex-preview/speech.pcm` (piper-synthesized 16 kHz mono s16le) against the live endpoint with configurable modes and prints a per-event timeline plus a GREEN/RED verdict on live transcripts. The opt-in suite test `CodexDictationTests::liveCodexDictationProvider` (env `SPEECHER_TEST_LIVE_CODEX_PCM=<pcm>`) runs the same check through the real transcriber and asserts partials arrive.
