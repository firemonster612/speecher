# Speech languages: what each service accepts (probed live 2026-10-03)

Decides the Spoken Language lists and request shapes. Probed with TTS clips
(edge-tts voices, 16 kHz mono s16) in German, French, Spanish, Japanese,
Italian, Portuguese, Korean, Chinese, Russian, Hindi, Polish and English, plus
short ambiguous clips and keyterm clips. Only accept/reject, event types,
lengths and whether the output's language and script matched the clip are
recorded here.

## Claude Voice (`wss://claude.ai/api/ws/speech_to_text/voice_stream`)

Query as Speecher sends it (`use_conversation_engine=true`,
`stt_provider=deepgram-nova3`, `forward_interims=typed`), only `language`
varied.

- **Validation.** The server checks `language` against its own list. A code it
  lacks closes the socket with **1003** and reason `harmonic provider does not
  support the requested language`, before any transcript event. Rejected:
  `multi`, `auto`, `xx-bogus`, and af as az ba bo br cy eu fo gl gu ha haw ht hy
  is jw ka kk km la lb ln lo mg mi ml mn mt my ne nn oc pa ps sa sd si sn so sq
  su tg tk tt uz yi yo nb.
- **Accepted base codes (52, plus `fil`):** am ar be bg bn bs ca cs da de el en
  es et fa fi fr he hi hr hu id it ja kn ko lt lv mk mr ms nl no pl pt ro ru sk
  sl sr sv sw ta te th tl tr uk ur vi yue zh. Regional tags were accepted too
  (en-GB en-AU en-IN en-NZ de-CH nl-BE sv-SE da-DK es-419 es-MX fr-CA pt-PT
  pt-BR ko-KR zh-TW zh-HK zh-Hans zh-Hant ja-JP de-DE en-US).
- **The final transcript follows the spoken language, whatever the hint.** A
  German clip came back German (script and language matched) with
  `language=en`, `ja`, `fr` and with `language` omitted; Japanese and Chinese
  clips came back in their own scripts with `language=en` or `de`.
- **The hint decides the live preview.** With a matching hint every clip
  streamed `TranscriptInterim` revisions (7 to 28 per clip) before
  `TranscriptText` and `TranscriptEndpoint`. With a mismatched hint (Japanese
  audio, `language=de`; Chinese audio, `language=en`) only one interim arrived,
  at the end, carrying the whole text.
- **Omitted `language` is accepted and detects.** Interims streamed for
  de fr es it pt ru pl hi ko ja en; Chinese got one interim at the end, as with
  a mismatch. Output matched the spoken language in every case.
- **Event types seen:** `TranscriptInterim`, `TranscriptText`,
  `TranscriptEndpoint`, `TranscriptError` (`error_code: rate_limit`,
  `description: Too many active sessions` when probing quickly, close **4029**).
- **Keyterms (`x-config-keyterms`).** The server decodes the header as UTF-8.
  Latin-1 bytes, which Speecher sent before, break non-ASCII terms: with
  `Schmiedtkë,Grüßling` sent as Latin-1 the German clip came back with the
  second word mangled (letters dropped where the ü and ß were). The same terms
  as UTF-8 came back intact, and a Japanese keyterm (渡邊) changed the output
  only when sent as UTF-8; without it, or percent-encoded, the model chose a
  more common spelling. Percent-encoding has no effect.
- Claude Code 2.1.287 itself sends `language` from a 20-code map
  (cs da de el en es fr hi id it ja ko nl no pl pt ru sv tr uk, default en) and
  strips non-ASCII keyterms.

## ChatGPT dictation (`wss://chatgpt.com/backend-api/dictation/stream`)

- **`session.start` `config.language`** is validated: an unknown code gets
  `session.error` `{"code":"invalid_event","message":"Value error, Invalid
  transcription language","retryable":false}` and no `session.started`.
  `auto` is rejected the same way.
- **Accepted (67 base codes):** af am ar az be bg bn bs ca cs cy da de el en es
  et fa fi fr gl gu he hi hr hu hy id is it ja ka kk kn ko lt lv mi mk ml mn mr
  ms my ne nl no pl pt ro ru sk sl so sr sv sw ta te th tl tr uk ur vi yue zh,
  plus nb, fil and the regional tags above. `session.started` echoes the code
  in `config.language`.
- **The hint never changed a transcript.** German audio with `language=en` or
  `ja`, Japanese with `de`, Dutch and German short clips with a wrong hint: the
  segments and `transcript.final` followed the spoken language, identical in
  length to the run with the matching hint or none. Event counts
  (`transcript.segment`, `session.updated`, `speech.started/stopped`,
  `asset.ready`) did not change either.
- **Omitted** (what Speecher sent before): detects; every clip matched.

## ChatGPT batch transcribe (`POST /backend-api/transcribe`)

- A `language` form field is read: a bogus value returns **500**. Valid values
  do not change the output: German audio with `en` or `ja`, and Japanese audio
  with `de`, returned the same text as with no field. 200 responses carry
  `text`, `asset_pointer`, `asset_ttl`, `asset_format`.

## Local Models (transcribe.cpp catalog at the pinned revision)

From `catalog/<variant>.json` (`languages`, `capabilities.lang_detect`):

| Model | Languages | Detects |
| --- | --- | --- |
| Moonshine Small, Medium | en | no |
| Parakeet 0.6B | en | no |
| Whisper Large v3 Turbo | 100 codes (Whisper's list) | yes |
| Qwen3-ASR 1.7B | 30 | yes |
| Cohere Transcribe | 14 | no |
| Voxtral Small 24B | 8 | yes |

transcribe.cpp rejects a hint outside the model's list with
`TRANSCRIBE_ERR_UNSUPPORTED_LANGUAGE` and detects when given none, where the
model can.

## What Speecher does with this

- Claude Voice lists the 52 accepted base codes plus Automatic, which leaves
  `language` out. A fixed code is still worth sending: it keeps the live
  preview streaming.
- ChatGPT Codex lists the accepted base codes (no duplicate `nb`/`fil`) plus
  Automatic, and sends `config.language` for a fixed one. The final
  re-transcription stays on: neither pass is held to the hint, so the batch pass
  cannot undo a language the stream kept.
- A Custom Endpoint lists Whisper's codes plus Automatic, which leaves the form
  field out.
- Filipino is stored as `tl` everywhere, the code both services and Whisper
  take. Qwen3-ASR declares it `fil`, so the local engine sends `fil` to a model
  that declares only that.
- Keyterms go to Claude Voice as UTF-8, capped at 1024 bytes of it, so no term
  is skipped for its characters.

## Probe harness

`.scratch/spoken-language/probe/` in the feature worktree: `claude.ts` (bun;
one Claude Voice stream per language and clip), `keyterms.py` (Python
websockets, writes the keyterm header as raw Latin-1, UTF-8 or percent-encoded
bytes), `codex.ts` (one `session.start` variant per run), `batch.ts` (the batch
endpoint), and the sweep scripts that drive them. Clips come from
`.scratch/spoken-language/tts/gen*.sh`.
