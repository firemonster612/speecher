# Concurrent speech sessions per account

Probed live: 2026-10-07, 20:00:51-20:01:28 UTC.

Issue: [#227](https://github.com/firemonster612/speecher/issues/227).

## Method

- Opened three Codex dictation WebSockets with the same stored access token.
  Started each next connection two seconds after the previous `session.started`,
  while the older connection kept streaming. Each received two copies of the
  existing espeak speech fixture from the 0004 context probe, followed by one
  second of silence. Sent 100 ms chunks at real-time speed, then `audio.flush`
  with `reason: "client"` and `session.close`.
- Audio was 16 kHz mono signed 16-bit little-endian PCM. The 265,754-byte
  fixture is 8.3048125 seconds long, SHA-256
  `22fb61410206ed53947ad66105e139ec38a7f1a62ed8ea54d1e0f263189e75c6`.
  Each connection received 17.609625 seconds, including silence. Total audio
  sent to Codex was 52.828875 seconds. Claude received none.
- Used the endpoint, bearer subprotocol and Chromium user agent in 0004 and
  `CodexDictationClient`. Requested `streaming_sse`, `segment`, English, a
  300,000 ms idle TTL and a 30,000 ms maximum utterance. Logged every server
  event, client finish, transport error and close code with relative timestamps.
- Read credentials directly, without invoking either client's credential
  loader or refresh path. Codex's access-token expiry was
  `2026-10-10T13:57:06Z`. Claude's stored `claudeAiOauth.expiresAt` was `0`,
  which Speecher's credential reader treats as expired. Per the probe's limits,
  stopped Claude before making any request. Neither credential file changed,
  verified by SHA-256 before and after each run. No refresh or keyring write.

## Codex findings

- Three simultaneous sessions work on the tested account. They received
  distinct `session.started` IDs at +0.733 s, +3.143 s and +5.570 s. All three
  fed audio concurrently until the first client finished at +18.554 s, about
  12.98 seconds of overlapping audio input.
- Opening newer sessions did not interrupt older ones. After session three
  started, sessions one and two produced another 39 and 48
  `transcript.segment` events respectively. Totals were 50, 52 and 52 segments.
  Every connection then received one nonempty `transcript.final` containing
  both copies of the speech, including "The quick brown fox" and "Kirigami
  form cards". This checks transcription, rather than just socket acceptance.
- Every connection followed the same finish sequence: the client sent
  `audio.flush` and `session.close`, the service emitted `speech.stopped`,
  `transcript.final`, then `session.updated` with `status: "closed"`, with
  active `session.updated` events between stop and final. The probe then closed
  the socket. All transport closes were code `1000` with an empty
  reason. There were no `session.error`, `transcript.failed`, socket errors,
  refusals or unsolicited closes in this run. It ended at +24.227 s.
- No quota, remaining-use, rate-limit or retry-delay information appeared in
  the successful run's events. The echoed buffer, utterance and idle-TTL
  settings are session configuration, not account usage limits.
- Successful handshake headers remain unobserved. Bun 1.3.0's `ws` layer
  reported that `upgrade` and `unexpected-response` events are unimplemented.
  A retry of the same probe under Node 22.22.2 was rejected on its first
  WebSocket handshake with HTTP `403`, `server: cloudflare`, and
  `cf-ray: a46f7f76fbf05bda-LIS`. It exposed no rate/limit/usage/retry headers.
  The local error was `WebSocket was closed before the connection was
  established`, followed by close `1006` with an empty reason. No
  `session.start` or audio was sent. This was a transport-dependent handshake
  rejection, not evidence of a concurrent-session limit.

## Claude findings and remaining unknowns

- Two and three concurrent Claude Voice sessions are unverified. The
  stored expiry metadata blocked both probes. No Claude server acceptance,
  refusal, replacement of an older session, later failure, or usage-limit
  information was observed. A future probe needs an existing unexpired login;
  this spike did not refresh or rewrite credentials.
- The Codex result supports mic, system audio and a dictation streaming
  together for a short run on this account. It does not establish a published
  concurrency guarantee, the maximum number of sessions, behavior on other
  accounts, or limits during an hour or more of continuous audio. No long
  recording or quota-exhaustion test was attempted.

## Probe artifacts

Uncommitted files under `.scratch/concurrent-speech/`:

- `probe.mjs`, run with `bun .scratch/concurrent-speech/probe.mjs`.
- `speech.pcm`, copied from the existing 0004 context-probe fixture.
- `codex-three.jsonl` and `results-bun.json`, the successful three-session run.
- `codex-three-node.jsonl` and `results.json`, the header-capture retry run with
  `node .scratch/concurrent-speech/probe.mjs`, rejected before sending audio.

Dependency `ws@8.22.0` was installed with Bun inside that scratch directory.
Only this research note is committed.
