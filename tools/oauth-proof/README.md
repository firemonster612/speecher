# M1 OAuth and WebSocket proofs

These Node 20+ ESM scripts have no npm dependencies. Run them on a host that
can make WebSocket connections. They deliberately write credentials outside the
repository, under `~/.speecher-m1/`, with file mode `0600`.

Run the sign-ins first:

```sh
node tools/oauth-proof/signin-anthropic.mjs
node tools/oauth-proof/signin-openai.mjs
```

Each command prints an authorization URL. Open it in any browser. The browser
may run on a different machine when the provider shows a `code#state` value:
paste that value into the terminal. A browser on the same host can instead
follow the loopback callback.

`signin-anthropic.mjs` listens on `127.0.0.1:54545/callback`, uses Claude's
public client id and PKCE, then POSTs JSON to
`https://platform.claude.com/v1/oauth/token`. Its JSON keys are ordered
`grant_type`, `code`, `redirect_uri`, `client_id`, `code_verifier`, `state`.
It sends `Content-Type: application/json`, `Accept: application/json, text/plain, */*`,
and `User-Agent: axios/1.15.2`, matching `OauthTokenRequest.h` and the flow
facts. It saves `anthropic.json` and prints only the first eight characters of
tokens, scopes, and expiry. A successful run ends with output like
`access_token: abcdefgh…`, `scope: ...`, `expires_at: ...`, and the saved-file
path.

`signin-openai.mjs` listens on `127.0.0.1:1455/auth/callback`, uses the Codex
public client id, PKCE, and the documented Codex authorize parameters. It sends
a form-encoded request to `https://auth.openai.com/oauth/token`, saves
`openai.json`, and decodes the unverified `chatgpt_account_id` claim from the
ID token. It also redacts token output.
A successful run prints the same token prefix, expiry, saved-file path, and a
`chatgpt_account_id` line.

Then give either WebSocket proof a 16 kHz, mono, PCM16 RIFF/WAVE file:

```sh
node tools/oauth-proof/ws-claude.mjs sample-16k-mono.wav
node tools/oauth-proof/ws-codex.mjs sample-16k-mono.wav
node tools/oauth-proof/ws-codex.mjs --final-retranscribe sample-16k-mono.wav
```

`ws-claude.mjs` reads `anthropic.json`, opens
`wss://claude.ai/api/ws/speech_to_text/voice_stream`, and supplies the query
from `ClaudeVoiceProtocol.cpp`: `linear16`, 16 kHz, one channel, the endpoint
timers, English, conversation engine, typed interims, and Deepgram Nova 3. It
sends the `Authorization: Bearer`, `x-app: cli`,
`anthropic-client-platform: linux`, and `Claude-Code` headers from
`ClaudeVoiceClient.cpp`. It sends the initial `KeepAlive`, 20 ms raw PCM binary
frames, then `{"type":"CloseStream"}`. It prints each received event and exits
after a transcript endpoint or error.

`ws-codex.mjs` reads `openai.json`, opens
`wss://chatgpt.com/backend-api/dictation/stream`, and uses the exact browser
user agent in `CodexDictationClient.h`. Its handshake subprotocols are
`chatgpt-dictation` and `openai-bearer.<token>`. It sends the `session.start`
configuration, base64 `audio.append` messages, then `audio.flush` and
`session.close`, exactly as `CodexDictationClient.cpp` does. It prints every
received event and exits when the server marks the session closed or sends an
error. `--final-retranscribe` enables the optional accuracy pass from
`CodexSpeechTranscriber.cpp`: a multipart `file=dictation.wav` request to the
transcribe endpoint with the same bearer token and browser user agent. A failed
or obviously truncated result leaves the streamed transcript in place.
For both WebSocket scripts, `TranscriptText`, `TranscriptInterim`,
`transcript.segment`, and `transcript.final` lines show that streaming worked;
`TranscriptEndpoint` or `session.updated` with `status: "closed"` is the clean
completion signal.

## Anthropic TLS-fingerprint result

The Anthropic script uses Node's stock TLS stack. A successful token exchange
that returns an access token proves Cloudflare does not require Claude Code's
TLS fingerprint for that authorization exchange from that host and IP. A 429,
403, challenge page, or connection rejection after the browser has supplied a
valid code is evidence that the stock Node fingerprint or surrounding request
identity was rejected. It is not conclusive on its own, because expired codes,
an incorrect callback, or account policy can produce similar failures. Record
the HTTP status and response body with token fields redacted, then retry once
with a newly obtained code before treating it as the TLS hazard.

The flow facts call out a conflict with the desktop refresh helper:
`OauthTokenRequest.h` uses JSON headers for every refresh request, including
OpenAI, while upstream OpenAI authorization-code exchange facts require a
form-encoded body. This proof follows the facts for OpenAI's authorization-code
exchange. Anthropic's facts and desktop helper agree on JSON and the axios user
agent.

Every fixed endpoint, OAuth client id, and exchange detail above comes from
`.scratch/android-client/oauth-flows.md`; WebSocket URLs, query parameters,
headers, user agent, and event framing come from the C++ files named in issue
#123.
