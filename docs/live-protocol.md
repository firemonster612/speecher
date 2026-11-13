# `speecher live --protocol jsonl`: the host protocol, version 1

`speecher live --protocol jsonl` runs one GPT-Live conversation for a host
app, such as a voice gateway or a call bot, for as long as the host wants it.
The host starts the process, writes commands to its stdin and reads events
from its stdout. Each is one JSON object on a line of its own. Audio travels
inside them as base64. stderr carries progress and failures for a log, never
anything the host has to parse.

```sh
speecher live --protocol jsonl --rate 16000 --voice cove \
  --instructions "You answer the phone for Rakazo. Keep answers short."
```

How GPT-Live itself behaves on the ChatGPT login is in
[docs/research/0010](research/0010-gpt-live-protocol.md). The command needs
a build configured with `-DSPEECHER_WITH_GPT_LIVE=ON` (OpenSSL, and
libdatachannel fetched at configure time); the release packages leave it out
for now.

## Versions

`session.started` carries `"protocol": 1`. The number changes only for a
change that would break a host written against it: a renamed or removed
event, command or field, or a field whose meaning changes. New events, new
commands and new fields can arrive within a version, so a host ignores
events and fields it does not know.

## Options

| Option | Meaning |
| --- | --- |
| `--rate <hz>` | The sample rate of `input.audio`, 8000 to 48000 (default 24000). |
| `--voice <name>` | juniper (default), breeze, cove, ember, maple, arbor, spruce, vale or sol. Fixed for the session. |
| `--instructions <text>`, `--instructions-file <path>` | How the assistant should behave. Fixed for the session; steer it later with `context.add`. |
| `--audio-out <file>` | Also write the assistant's audio to a WAV file, and leave `assistant.audio` out of the events. |
| `--echo-cancel` | Take the assistant's audio, as speecher sent it, out of the input; see Echo. |
| `--accept-slow` | Keep a session GPT-Live runs at half speed; see Starting. |

## Commands (host to speecher)

| Command | Fields | Effect |
| --- | --- | --- |
| `input.audio` | `audio`: base64 s16le mono PCM at `--rate` | The caller's audio. Send it as it is captured, at least every 100 ms, silence included. |
| `input.mute` / `input.unmute` | | GPT-Live stops or starts hearing the caller. Answered `input.muted` or `input.unmuted`. |
| `context.add` | `text` | Text the assistant acts on as if told it, from facts ("The caller is Sam") to prompts ("Greet the caller"). Answered `context.added`. |
| `playback.stop` | | Drops the rest of the assistant's current turn from the events. GPT-Live has no way to cancel a reply from the client, so it goes on, unheard, and thinks the caller heard it. |
| `session.end` | | Ends the session: `session.closed` follows, then the process exits. Closing stdin does the same. |

An unknown command or a line that is not JSON gets an `error` event; the
session goes on.

## Events (speecher to host)

Times (`start_ms`, `end_ms`) are milliseconds of session time, from the
start of the caller's audio.

| Event | Fields | Meaning |
| --- | --- | --- |
| `session.started` | `protocol`, `input_rate`, `output_rate` | The first event. Send `input.audio` from here on. |
| `user.speech_started` | `start_ms` | The caller started a turn. |
| `user.transcript.delta` | `text`, `start_ms`, `end_ms` | A word or two of the caller, as heard. |
| `user.turn` | `text`, `start_ms`, `end_ms` | The caller's whole turn. |
| `assistant.speech_started` | `start_ms` | The assistant started a turn. |
| `assistant.audio` | `pcm_s16le_24k` (base64, 24 kHz mono s16le), `ms` | The assistant's audio, a 200 ms chunk at a time, as GPT-Live plays it: in real time, not ahead of it. Silence between turns is not sent. |
| `assistant.transcript.delta` | `text`, `start_ms`, `end_ms` | The assistant's words as it says them. |
| `assistant.turn` | `text`, `start_ms`, `end_ms` | The assistant's whole turn, as far as it was spoken. |
| `assistant.interrupted` | `text`, `heard_ms`, `start_ms`, `end_ms` | The caller talked over the assistant, and GPT-Live cut its turn short. `text` is what was said and `heard_ms` how long it ran. **Flush the playback buffer now.** |
| `input.muted`, `input.unmuted`, `context.added` | | Acknowledgements. |
| `usage` | `audio_ms` | Audio the session has heard. |
| `error` | `message` | Something went wrong that the session survived: a rejected command, or a slow session redialled. |
| `session.closed` | `reason` | Always the last event. `client_request` after `session.end`; `connection_closed` when GPT-Live hung up, or did not answer the close within 5 seconds, without saying goodbye. |

The process exits 0 after a session that ran and closed, and 1 when it could
not start or broke off. A session GPT-Live ended by itself counts as broken
off. It exits 2 for a usage error.

## Conversations

GPT-Live is full duplex and decides turns itself: there are no voice
activity settings to tune. GPT-Live refuses them, as it refuses changes to
the voice or instructions mid-session. A host just keeps streaming the
caller's audio.

- **Answers.** The assistant answers once the caller stops: about 0.5 s
  after the end of a complete sentence, longer when the caller sounds
  unfinished. It may back-channel ("Mm-hmm") while the caller talks.
- **Interruptions.** When the caller talks over the assistant, GPT-Live
  stops it about a second after the caller starts. The audio already sent was
  played in real time, so what it said is what was heard, and GPT-Live's own
  record of the turn ends there too. speecher sends `assistant.interrupted`
  as soon as the turns show it, a few milliseconds after `assistant.turn`.
  The host drops whatever it still holds of that turn's audio.
- **Playback.** Play `assistant.audio` as it comes, behind a small jitter
  buffer (100 to 200 ms), and flush on `assistant.interrupted`. A deeper
  buffer plays audio the caller has already interrupted.

## Starting

About a third of GPT-Live sessions take audio in at half speed. Their
answers come late and their audio arrives too slowly to play. speecher
times the first 2 s of each session. On a slow one it says so in an `error`
event and calls again, up to three times, before `session.started`. This
adds about 2.5 s to every start. `--accept-slow` turns it off.

## Echo

The assistant hearing its own voice back from the host's speaker must not
count as the caller talking. On 2026-11-12, GPT-Live ignored its own audio
mixed back into the input, even at full volume 300 ms late. It transcribed
none of it and never cut its turn short. So a host whose echo is the
assistant's audio, delayed and attenuated, needs to do nothing.
`--echo-cancel` adds Speecher's WebRTC echo canceller, with the assistant's
audio as speecher sent it as the reference, for rooms or lines that distort
the echo. It assumes the host plays the audio as it arrives. A host that can
do neither plays with `input.mute` on and gives up interruptions.
