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

## Session TTL (probed live 2026-10-02)

- `session_ttl_ms` is an inactivity timeout, not a session lifetime. Every
  `session.updated` carries `last_activity_ms` and `expires_at_ms =
  last_activity_ms + session_ttl_ms`, and each `audio.append` (digital
  silence included) moves `last_activity_ms`. A session fed audio
  continuously stays open: 420 s at the shipped 300 s TTL, and 150 s at a
  12 s TTL, closed only on `session.close`.
- Expiry is silent. An idle session got no event past its TTL (watched for
  2 min at an 8 s TTL); the socket stays open, and the next `audio.append`
  gets `session.error` `{"code":"session_not_found","retryable":true}` with
  `fatal: true`, then a 1000 close. Audio sent to an expired session is lost.
- A 100 ms `audio.append` of zeros before `expires_at_ms` keeps the session
  alive; speech after a 20 s pause at an 8 s TTL then transcribed in full.
- Two sessions on one token can stream at the same time.

## Long files (probed live 2026-11-13)

A 169 s file of continuous speech (Kokoro, sentence pauses well under a
second), streamed at Speecher's file pace of 100 ms every 12 ms:

- The server's VAD never ended the utterance: one `speech.started` at 0.5 s,
  and `speech.stopped` only at the final `audio.flush`.
  `max_utterance_duration_ms: 30000` split nothing.
- The ASR reads an utterance at its own pace, slower than the audio arrives:
  `transcript.segment` text reached about 100 s of audio when all 169 s were
  sent (20.7 s in), and kept growing after: about 5 times real time. In
  Speecher's runs it stopped earlier, in paragraph nine, so its pace varies.
- 15 s after the flush, with about 10% of the audio still unread, came
  `transcript.failed` `{"code":"BidiFlushTimeoutError","message":"Streaming
  ASR failed before producing a final transcript.","retryable":true}`, then
  `session.updated` closed. No `transcript.final` came for any of it. Twice
  in two runs.
- With an `audio.flush` every 20 s of audio, each flush ended the utterance
  and the next append started a new one. The server read them side by side:
  all nine finals came, the last 3.9 s after `session.close` (24.7 s in all),
  and together they held the whole text. They came out of order: a short last
  utterance's final came before the one ahead of it.

- Flushed at the first 100 ms 20 dB under the loudest past 20 s, three of
  seven joins repeated a word: one final ended "…nothing gets cut off." and
  the next began "Cut off at the end". That cut fell on a lone quiet 100 ms
  inside the phrase; the sentence's pause came 0.8 s later.
- Flushed only after 300 ms that quiet, the joins were clean in two runs,
  and so was Speecher's own transcript of the 187 s MP3 in three: every word
  once, in order, one misheard.

So a client sends a long run as utterances short enough to be read within
15 s of their flush, cut at a pause of 300 ms or more, and puts the finals
back in the order of `speech.started`. Speecher cuts at the first such pause
past 20 s of each utterance, at latest at 40 s.

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

`.scratch/codex-long-audio/probe.ts` (bun) streams a PCM file at a set pace, with an optional flush every N seconds, and logs each event with how much audio was sent; it ran the long-file probes. `.scratch/codex-context-probe/{probe,sweep,update,update2,full,batch,batch2,batch3}.ts`
(bun; `full.ts` streams `speech.pcm`, espeak-synthesized 16 kHz mono s16le;
`batch*.ts` hit the backend transcribe endpoint) cover the probes above: each opens a live socket, sends one `session.start` /
`session.update` variant, and prints the server's verdict. The original
`.scratch/codex-preview/probe.ts` (bun) replays `.scratch/codex-preview/speech.pcm` (piper-synthesized 16 kHz mono s16le) against the live endpoint with configurable modes and prints a per-event timeline plus a GREEN/RED verdict on live transcripts. The opt-in suite test `CodexDictationTests::liveCodexDictationProvider` (env `SPEECHER_TEST_LIVE_CODEX_PCM=<pcm>`) runs the same check through the real transcriber and asserts partials arrive.
