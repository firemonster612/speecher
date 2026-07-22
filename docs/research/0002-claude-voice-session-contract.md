# Claude Voice session and output contract

Date: 2026-07-22

Decision ticket: [#5](https://github.com/firemonster612/speecher/issues/5)

Status: decision-ready research; no product code changed

## Decision

Keep the existing provider split, but make the Dictation Session—not the Claude adapter—the owner of transcript lifecycle, retry policy, captured audio, and output choice.

The initial speech path can continue to use the user's Claude.ai OAuth session against Claude Voice. The observed connection is to a Claude endpoint authenticated with the Claude bearer token; `stt_provider=deepgram-nova3` is a server-side selection and does not require a user-supplied Deepgram credential. This is an undocumented Claude Code protocol, however, so it must be treated as a replaceable compatibility adapter rather than a stable public API.

The minimum contract change is to distinguish:

1. replaceable working transcript revisions shown while listening;
2. provider-final transcript segments;
3. completion of the whole speech attempt after input is closed;
4. typed failure with enough phase/retry information for the session to attempt one replay;
5. explicit cancellation, separate from finishing input.

The current `partialTranscript`, `finalTranscript`, `failed`, and ambiguous `stop()` interface has no whole-attempt completion signal ([`DictationInterfaces.h:71-93`](../../src/dictation/DictationInterfaces.h)). The session therefore waits an arbitrary 650 ms after `stop()` and can refine or deliver a still-interim value ([`DictationSession.cpp:248-275`](../../src/dictation/DictationSession.cpp)). That cannot guarantee a final Raw Transcript after stop.

## Evidence and confidence

The provider observations below come from two independent local artifacts:

- Speecher at starting commit `b891fbb`, especially its provider and session code.
- The installed Claude Code 2.1.215 Linux executable at `/home/efox/.local/share/claude/versions/2.1.215`, SHA-256 `c1efffaaf370aa187cb6a09dd93d4e511c646899b0078476f83791b664bde7fe`. Its embedded client was inspected statically; no credentials were printed and no live request was made.

The installed-client evidence is stronger than inference from Speecher, but it is still client-artifact evidence, not proof of current server behavior. Claude Code is independently documented as supporting Claude App Pro/Max login through a Claude.ai account ([Anthropic, “Set up Claude Code”](https://docs.anthropic.com/en/docs/claude-code/getting-started)). Anthropic does not publicly document this voice WebSocket contract.

## What the current path supports

### Transport and authentication

Both implementations construct a WebSocket under `/api/ws/speech_to_text/voice_stream`. Speecher defaults the base to `https://claude.ai`, converts it to `wss`, loads or refreshes credentials from the Claude Code credential file, and sends the access token as `Authorization: Bearer ...` ([`SettingsStore.cpp:459-471`](../../src/core/SettingsStore.cpp), [`ClaudeSpeechTranscriber.cpp:12-96`](../../src/providers/ClaudeSpeechTranscriber.cpp), [`ClaudeVoiceClient.cpp:184-204`](../../src/providers/ClaudeVoiceClient.cpp)). No Deepgram key appears in this path.

The installed 2.1.215 client and Speecher agree on these stream parameters:

| Field | Value/effect |
| --- | --- |
| `encoding` | `linear16` |
| `sample_rate` | `16000` |
| `channels` | `1` |
| `endpointing_ms` | `300` |
| `utterance_end_ms` | `1000` |
| `language` | `en` for the settled English-only path |
| `use_conversation_engine` | `true` |
| `forward_interims` | `typed` when enabled |
| `stt_provider` | `deepgram-nova3` |

Speecher defines these at [`ClaudeVoiceClient.cpp:117-134`](../../src/providers/ClaudeVoiceClient.cpp) and tests the query shape at [`test_core.cpp:2447-2462`](../../tests/test_core.cpp). Audio is sent as binary PCM frames. `KeepAlive` and `CloseStream` are JSON text frames ([`ClaudeVoiceClient.cpp:141-170`](../../src/providers/ClaudeVoiceClient.cpp), [`ClaudeVoiceClient.cpp:235-255`](../../src/providers/ClaudeVoiceClient.cpp)).

There is one known protocol drift. Claude Code 2.1.215 normalizes, deduplicates, comma-joins, and caps keyterms to 1,024 ASCII characters, then sends them in `x-config-keyterms`. Speecher sends repeated `keyterms` query values ([`ClaudeVoiceClient.cpp:130-133`](../../src/providers/ClaudeVoiceClient.cpp)). Whether the server still honors the older query form needs a live test; the test named `claudeVoiceStreamQueryMatchesClaudeCode` currently proves only Speecher's own expected query.

### Transcript events and finalization

The installed client handles four explicit response types:

- `TranscriptInterim` and `TranscriptText`: `data` is a replaceable working revision, not a final segment.
- `TranscriptEndpoint`: promote the most recent working revision to a final segment.
- `TranscriptError`: fail the attempt, preserving the most recent working revision internally.
- `error`: fail the attempt.

On finish, the installed client sends `CloseStream` and waits for a post-close endpoint. It also has a 1.5-second no-data timeout and a 5-second safety timeout; before resolving via a timeout or close, it promotes an unreported interim. Speecher similarly promotes its latest interim when it sees `TranscriptEndpoint` and has a 5-second force-close timer ([`ClaudeVoiceClient.cpp:303-352`](../../src/providers/ClaudeVoiceClient.cpp)), but it does not expose finalization completion to `DictationSession`.

The current `TranscriptState` is useful as a raw revision accumulator: final segments are appended and the current partial is replaced ([`TranscriptState.cpp:10-52`](../../src/core/TranscriptState.cpp)). It is not sufficient as the domain result because `text()` includes the current partial. The session reads that same value for preview, bindings, refinement, empty-result detection, and delivery ([`DictationSession.cpp:87-92`](../../src/dictation/DictationSession.cpp), [`DictationSession.cpp:390-455`](../../src/dictation/DictationSession.cpp)). A partial can therefore become the effective Raw Transcript without a provider completion boundary.

### Cleanup

The cleanup boundary can mostly stay. `TranscriptRefiner` is already provider-neutral and exposes streaming deltas plus completed/failed results ([`DictationInterfaces.h:95-119`](../../src/dictation/DictationInterfaces.h)). OpenAI and Anthropic are registered independently of the speech provider ([`ApplicationController.cpp:142-152`](../../src/app/ApplicationController.cpp)); disabling refinement already falls back to the post-binding raw path, and a refiner failure also falls back rather than losing the dictation ([`DictationSession.cpp:406-455`](../../src/dictation/DictationSession.cpp), [`DictationSession.cpp:562-578`](../../src/dictation/DictationSession.cpp)).

The existing `light_cleanup`, `balanced`, and `strong_polish` values already behave as cleanup strength. Preserve those persisted values, but expose the concept as `cleanupStrength` rather than overloading `style` ([`SettingsStore.cpp:296-312`](../../src/core/SettingsStore.cpp)). This is a naming/model clarification, not a reason to replace the refiner interface. Anthropic can continue to route through either a Claude Code session or direct OAuth mode, while OpenAI retains its own auth/provider path ([`AnthropicTranscriptRefiner.cpp:90-142`](../../src/providers/AnthropicTranscriptRefiner.cpp), [`OpenAiTranscriptRefiner.cpp:72-100`](../../src/providers/OpenAiTranscriptRefiner.cpp)).

## State ownership and lifecycle

Use one in-memory session record with separate fields; do not reuse one mutable string for every stage.

| State | Owner | Meaning |
| --- | --- | --- |
| captured PCM and retry count | Dictation Session | Full audio for the current session only; discarded after terminal delivery/error |
| working raw revision | current speech attempt under session ownership | Provider finals accumulated plus the replaceable interim; drives the live popup only |
| final Raw Transcript | Dictation Session | Frozen only after the speech attempt completes; input to bindings and optional cleanup |
| binding/placeholder text | binding stage | Internal derived data, not a transcript exposed to the user |
| Refined Transcript | Dictation Session | Optional completed cleanup result; never overwrites the final Raw Transcript |
| delivery representation | output formatter/delivery layer | Plain text plus optional sanitized HTML generated from the chosen final transcript |

The terminal flow should be:

```text
Listening
  -> stop capture / finish speech input
  -> wait for speech-attempt completion
  -> freeze and continue showing final Raw Transcript
  -> optional binding + cleanup
  -> choose Raw or Refined Transcript
  -> render requested output format
  -> deliver
```

Starting a retry creates a fresh attempt accumulator. It must not append the second attempt's segments to the first attempt's partial transcript. Generation/attempt identity is required so late frames from the closed socket cannot mutate the active result.

The settings and per-session overrides also belong to the session snapshot. Speecher snapshots settings at start but reads them again before refinement and delivery ([`DictationSession.cpp:174-205`](../../src/dictation/DictationSession.cpp), [`DictationSession.cpp:406-464`](../../src/dictation/DictationSession.cpp)). A single immutable `SessionOptions` captured at start prevents mid-session settings changes and CLI overrides from being lost.

The popup wiring can stay, but its stage policy must change. Today it receives only the last configured number of words and is explicitly hidden when refinement starts ([`DictationSession.cpp:87-92`](../../src/dictation/DictationSession.cpp), [`DictationSession.cpp:441-444`](../../src/dictation/DictationSession.cpp)). It should instead show the working Raw Transcript while listening, then freeze the completed Raw Transcript after stop; refinement deltas are a separate state and should not replace that display.

## Small provider contract shape

Names are illustrative; the required semantics matter more than this exact C++ spelling.

```cpp
struct SpeechRevision {
    quint64 attemptId;
    QString text;
    bool segmentFinal;
};

struct SpeechFailure {
    quint64 attemptId;
    QString phase;       // connect, streaming, or finalize
    bool retryable;
    QString message;
};

class SpeechTranscriber {
    startAttempt(attemptId, speechSettings);
    sendAudio(attemptId, pcm);
    finishInput(attemptId); // request provider finalization
    cancelAttempt(attemptId);

signals:
    revision(SpeechRevision);
    attemptCompleted(attemptId);
    attemptFailed(SpeechFailure);
};
```

`segmentFinal` preserves the observed endpoint behavior without pretending the provider returns one atomic transcript. `attemptCompleted` is deliberately separate: a final segment does not prove that no more provider events are coming. `finishInput` and `cancelAttempt` replace the two meanings currently hidden behind `stop()`.

The Claude adapter should parse the observed event types explicitly and ignore unknown frames with schema-only diagnostics. The current recursive `firstTranscriptText` and name-based `messageLooksFinal` parser ([`ClaudeVoiceClient.cpp:18-68`](../../src/providers/ClaudeVoiceClient.cpp)) is tolerant during discovery, but it can silently reinterpret future unrelated JSON as transcript text. Compatibility fallbacks can remain opt-in diagnostics, not the authoritative contract.

## One full-audio retry

Neither Speecher nor Claude Code 2.1.215 currently provides the settled full-audio retry.

Speecher retains at most 15 seconds only while the socket is connecting, flushes that buffer on connect, and clears it on stop ([`ClaudeVoiceClient.cpp:222-300`](../../src/providers/ClaudeVoiceClient.cpp)). The installed Claude client retries one early pre-transcript stream error, but it likewise clears its connection buffer after flushing; this is a reconnect safeguard, not replay of the whole recording.

A full retry therefore requires the Dictation Session to retain every emitted PCM chunk independently of provider buffering. At 16 kHz, mono, signed 16-bit PCM, storage is about 32 KB/s (about 1.9 MB/minute). On a retryable connect/stream/finalize failure before authoritative completion, the session should:

1. cancel/retire attempt 0;
2. create attempt 1 with a new identifier;
3. open a fresh Claude Voice stream;
4. replay the captured PCM in order, then `finishInput`;
5. permit no further automatic attempt.

The protocol shape makes such a replay plausible because a fresh stream accepts binary linear16 frames followed by `CloseStream`. It is **not proven** that the service accepts recorded audio replayed faster than real time, what pacing it requires, or that identical audio produces a complete endpoint. Those are live-test gates. Until they pass, the contract may promise one attempted replay, not guaranteed recovery through this provider.

Do not classify authentication rejection, unsupported protocol/schema, or user cancellation as retryable. Exact transient status/code classification needs the live test because the current adapter collapses most WebSocket errors into strings.

## Plain-text and HTML output

Output format is distinct from output method. `automatic`, `ydotool`, `wl-copy`, and Qt clipboard choose a delivery backend; they do not describe the content representation ([`TextDelivery.cpp:246-287`](../../src/output/TextDelivery.cpp)). Add a persisted default `plain`/`html` format plus an optional per-session CLI override. The override must be carried in the start/toggle IPC request and applied to `SessionOptions`; the current CLI and IPC support only command strings and have no argument payload ([`main.cpp:77-127`](../../src/app/main.cpp), [`ApplicationController.cpp:121-139`](../../src/app/ApplicationController.cpp)). It must not rewrite the persistent default.

Represent rich output as a value, not as an HTML-looking `QString`:

```cpp
struct DeliveryContent {
    QString plainText;           // always present and authoritative fallback
    std::optional<QString> html; // sanitized UTF-8 fragment when requested
};
```

Raw and Refined Transcripts remain text. HTML is generated only at the output boundary from permitted structure, then sanitized with a small element/attribute allowlist. Never put unsanitized provider/model HTML on the clipboard, and never send markup characters to a plain-text typing backend. When HTML is requested, advertise both `text/plain` and `text/html` where the backend can do so; Qt's `QMimeData` is explicitly designed to hold multiple representations and provides `setText()` and `setHtml()` ([Qt `QMimeData` documentation](https://doc.qt.io/qt-6/qmimedata.html)). A backend that cannot reliably offer both representations must fall back to `plainText` and report that downgrade.

This requires broadening `TextDeliveryAdapter` and `DeliveryBackend`, which currently accept only a `QString` ([`DictationInterfaces.h:121-127`](../../src/dictation/DictationInterfaces.h), [`TextDelivery.h:12-27`](../../src/output/TextDelivery.h)). The current concrete backends are plain-only: Qt calls `clipboard->setText`, and `wl-copy` is forced to `text/plain` ([`QtClipboardDelivery.cpp:16-35`](../../src/output/QtClipboardDelivery.cpp), [`WlClipboardDelivery.cpp:144-147`](../../src/output/WlClipboardDelivery.cpp)). Clipboard snapshot/restore already retains arbitrary MIME bytes and Qt HTML, so that part can stay ([`TextDelivery.cpp:42-102`](../../src/output/TextDelivery.cpp), [`WlClipboardDelivery.cpp:149-232`](../../src/output/WlClipboardDelivery.cpp)).

## What can stay and what must change

| Area | Disposition |
| --- | --- |
| provider registry and separate speech/refinement adapters | Stay |
| Claude credential load/refresh and Claude.ai WebSocket path | Stay behind an explicitly undocumented compatibility adapter |
| English linear16/16 kHz/mono stream settings | Stay for the initial path |
| optional `none`/OpenAI/Anthropic cleanup providers | Stay |
| three persisted cleanup levels | Stay, clarified as cleanup strength |
| `TranscriptState` append-final/replace-partial mechanics | Reuse inside one speech attempt, but split working and completed access |
| popup preview plumbing | Stay, with working-raw then frozen-final-raw stage semantics |
| `stop()` plus 650 ms timer | Replace with finish/cancel and attempt completion |
| provider-local 15-second connection buffer | Stay as a connection buffer; it is not the retry buffer |
| full-session PCM storage and one-retry policy | Add to Dictation Session |
| settings re-snapshot during refinement/delivery | Replace with immutable per-session options |
| command-only CLI/IPC | Extend with an optional output-format field |
| string-only delivery contract | Replace with plain + optional sanitized HTML content |
| clipboard restoration | Stay, generalized to the new outgoing MIME bundle |

## Required live provider validation

Run these against a test Claude.ai account/session before treating the adapter as production-stable. Record only frame types, keys, byte counts, timing, close codes, and hashes—not transcript or token contents.

1. Confirm the current base URL, required headers, and whether Claude.ai Pro/Max OAuth is sufficient without any Deepgram credential.
2. Capture the exact JSON keys for `TranscriptInterim`, `TranscriptText`, `TranscriptEndpoint`, `TranscriptError`, and `error`; verify that `data` is always the transcript field.
3. Determine whether `TranscriptText` is always non-final and whether every `CloseStream` yields a final `TranscriptEndpoint`.
4. Measure finalization timing for short speech, trailing silence, immediate stop, and multiple pause-delimited endpoints; set the session timeout from evidence rather than 650 ms.
5. Verify whether `x-config-keyterms` is required, whether repeated `keyterms` query parameters still work, and the effective limits/normalization.
6. Replay a complete in-memory recording on a fresh stream both unpaced and real-time paced; verify acceptance, final endpoint, ordering, and duration limits.
7. Exercise connect failure, mid-stream disconnect, post-`CloseStream` disconnect, 401/403, rate limit, and server error to derive typed retryability.
8. Verify that late frames from attempt 0 cannot arrive after attempt 1 starts and mutate the second accumulator.
9. Re-run the matrix whenever the installed Claude Code voice contract changes; pin the observed Claude Code version/hash in the test record.

## Consequences

This contract delivers the settled behavior without coupling session semantics to Deepgram or to Claude's private frame names. It also makes a future documented cloud or local speech provider implement the same attempt lifecycle. The cost is one intentional expansion of session state and delivery content types. The largest unresolved risk is external: the Claude Voice endpoint is undocumented and can drift independently of Speecher, so schema diagnostics and a live compatibility test remain release gates.
