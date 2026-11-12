# GPT-Live over the Codex ChatGPT login (verified live 2026-11-12)

GPT-Live (`gpt-live-1-codex`) is reachable with the same ChatGPT OAuth login the
`codex` speech provider uses, but only as a WebRTC call. The plain websocket
GPT-Live endpoints refuse that login. Everything under "Verified" was observed
on 2026-11-12 from happy with a ChatGPT Pro Lite login; "Not verified" lists
what these probes did not settle.

Sources: the probes below, and the Codex CLI source at `rust-v0.167.0`
(`codex-rs/core/src/realtime_conversation.rs`,
`codex-rs/codex-api/src/endpoint/realtime_call.rs`,
`codex-rs/codex-api/src/endpoint/realtime_websocket/*frameless_bidi*.rs`),
where GPT-Live is the "frameless bidi" (`RealtimeWsVersion::V3`) wire format.

## Verified

### No websocket-only session

- `wss://chatgpt.com/backend-api/codex/live?model=gpt-live-1-codex` and
  `…/codex/realtime?model=…` answer the websocket upgrade with `403` and an
  empty body. The same headers upgrade `…/codex/responses` with `101`, so the
  login and headers are fine; these routes refuse them.
- This matches Codex: its websocket path calls `realtime_api_key()`, which
  needs an API key (or `OPENAI_API_KEY`) and otherwise fails with "realtime
  conversation requires API key auth", with a TODO to drop that requirement
  for ChatGPT sign-in.

### Creating a call

`POST https://chatgpt.com/backend-api/codex/realtime/calls?intent=quicksilver&architecture=avas`

- Headers: `Authorization: Bearer <codex access token>`,
  `openai-alpha: quicksilver=v2`, `Content-Type: application/json`. Without
  `openai-alpha`, or without the query, the answer is `400`; without
  `Authorization`, `401`. `ChatGPT-Account-ID`, `originator` and a browser
  User-Agent were not needed (Codex sends the first two).
- Body: `{"sdp": "<offer>", "session": {…}}`, Codex's "backend request
  shape" for a `/backend-api` base URL (the paid API takes multipart instead).
- `201`, `Content-Type: text/plain`: the body is the SDP answer, and
  `Location: /v1/realtime/calls/rtc_…` carries the call id.
- An offer with no audio section gets `400 invalid_offer`, "Offer did not have
  an audio media section." It is checked before anything else.

### The session object

- `instructions` (string) works alone.
- `model`: `gpt-live-1-codex` and `gpt-live-1` are both accepted; leaving it
  out works too.
- `audio.output.voice` takes ChatGPT Voice names: `juniper`, `breeze`,
  `cove`, `ember`, `maple`, `arbor`, `spruce`, `vale` and `sol` each started
  a session. The API voices `marin` and `cedar` did not: the call still
  answers `201`, but the session never comes up. ICE gets no answer, and the
  sideband says `404 session_id_not_found`. A bad session object fails
  silently after `201`, so a client must treat "no `session.started`" as the
  failure.
- The voice is fixed at creation: a later `session.update` gets "Voice cannot
  be updated after initialization." `session.voice` is an unknown parameter.
- `delegation: {"type": "client"}`, which Codex sends, is accepted.

### The WebRTC answer

- `a=ice-lite`, `a=setup:passive`, so the client is ICE-controlling and the
  DTLS client. Candidates are UDP `…:3478` host candidates on four addresses
  plus TCP passive on `:443`. The ICE ufrag contains `/` (`vH9W8Y/u24/…`).
- Audio codecs offered back: Opus 48 kHz stereo (96), PCMU (0), PCMA (8).
- An offer with PCMU alone is answered with PCMU alone and works end to end.
  The user transcript was identical to the Opus run.
- An offer with no data channel works: the data channel is optional.
- With aiortc the peer connected within ~0.3 s of applying the answer.

### The sideband websocket

`wss://api.openai.com/v1/live/<call id>` with the same ChatGPT bearer and
`openai-alpha: quicksilver=v2`. It opens even when the WebRTC peer never
connects, and starts with
`{"type":"session.started","session":{"id":"rtc_…","status":"active","expires_at":…}}`,
where `expires_at` is about 120 minutes on. `wss://chatgpt.com/backend-api/codex/live/<call id>`
answers `403`.

Server events seen (the data channel, when there is one, carries the same
events except the two audio ones):

| Event | Fields | Notes |
| --- | --- | --- |
| `session.input_audio.append` | `audio`: base64 | The call's input, echoed: 24 kHz mono s16le, 200 ms (9600 bytes) each. |
| `session.output_audio.delta` | `delta`: base64 | The assistant's audio, 24 kHz mono s16le, continuous: zeros while it is silent. Transcribed locally, it matched the assistant's transcript word for word. |
| `input_transcript.added` | `item.text`, `start_ms`, `end_ms` | A word or two of the user, as heard. |
| `turn.created` | `turn.{id, role, transcript, start_ms, end_ms}` | role `user` or `assistant`. |
| `turn.delta` | `turn_id`, `delta`, `start_ms`, `end_ms` | Growing turn text. |
| `output_transcript.added` | `item.text`, `start_ms`, `end_ms` | The assistant's words as it speaks. |
| `turn.done` | `turn.{id, role, transcript, …}` | The whole turn's text. |
| `session.usage.updated` | `usage.audio_duration_ms`, `usage_limit.{status, reset_seconds}` | The plan's limit. |
| `session.closed` | `reason` (`client_request`), `usage` | Last event. |
| `error` | `error.{type, code, message}` | Per rejected client event; the session goes on. |

Client events the sideband accepts, from its own error message:
`session.update`, `output_audio.playback.play`, `session.feedback`,
`input_audio.pause`, `input_audio.resume`, `session.context.append`,
`response.item.create`, `response.create`, `delegation.context.append`,
`delegation.function_call_output.create`, `transport.dtmf.send`,
`session.close`.

- There is no audio input on the sideband: `input_audio.append` is rejected.
  Audio reaches the model only as WebRTC RTP.
- `response.item.create` and `response.create` answer "requires a session
  with Responses delegation", so text input needs a delegation this probe did
  not set up.
- `session.close` ends the session with `session.closed`.

### One exchange

JFK's 11 s clip as the microphone. The user turn went `turn.created`, then
word-level `input_transcript.added` and `turn.delta` about 0.4 s apart, then
`turn.done`: "And so, my fellow Americans, ask not what your country can do
for you, ask what you can do for your country". The assistant began ~0.5 s
after the speech ended, with `output_transcript.added` words. Its
`turn.done`: "Mm-hmm. Yep, that's JFK's inaugural line from 1961. It's one of
those defining calls to civic duty. What made you pull that up just now?"
GPT-Live answered on its own once the user stopped. The client sent no
commit or `response.create`.

## What a client needs

1. A WebRTC peer that can do ICE as the controlling agent against an ICE-lite
   server, DTLS-SRTP as the client, and send RTP. PCMU needs no codec library;
   Opus needs libopus. No data channel, and inbound RTP can be ignored.
2. One HTTPS POST with the offer.
3. One websocket for everything else: transcripts, turns, usage, errors and
   the assistant's audio as PCM, so nothing has to be decoded from RTP.

## Not verified

- Barge-in: what the server does when the user talks over the assistant
  (whether `output_audio.delta` stops, or a turn is cut short).
- Sessions near the 120 minute expiry, idle timeouts, and how many sessions
  one login may hold.
- What `usage_limit.status` says once the plan's limit is reached.
- Whether the chosen voice is audible as that voice; only "the session
  starts" was checked.
- Whether a Business or Enterprise workspace needs `ChatGPT-Account-ID`.
- ICE over TCP 443 alone, for networks that block UDP.
- Text input through Responses delegation, `session.context.append`'s effect,
  and `transport.dtmf.send`.

## Probe harness

`.scratch/gpt-live-probe/` (locally excluded, as for 0004), run with
Python 3.14, aiortc 1.16 and websockets in a throwaway venv. It reads the
login from `~/.codex/auth.json` and redacts the token and account id from
every log.
- `handshake.ts` and `upgrade.sh`: websocket upgrades and their statuses.
- `calls.ts`: call creation with an invalid offer.
- `call.py <wav> <dir> [model] [linger]`: one call with the WAV as the
  microphone. Takes `SESSION_JSON`, `CODEC=PCMU`, `NO_CHANNEL=1` and
  `SIDEBANDS`. Writes `events.jsonl` (data channel and sideband),
  `assistant.wav` (RTP), `offer.sdp` and `answer.sdp`.
- `sideband_only.py <wav24k> <dir> [input-event] [linger] [connect-peer]`:
  a call without a connected peer, talking over the sideband alone.
  `AFTER_START` takes events to send.
- `headers.py`: which headers call creation needs.
- `summary.py <events.jsonl>`: the notable lines of a run.
