# Local models and custom endpoints

Status: accepted, 2026-09-25

## Context

Every speech and refinement provider today is a cloud service reached through
a Claude or ChatGPT sign-in. Two groups are left out: people with neither
subscription, and people who want dictation to stay on their machine. Some of
them already run their own servers (whisper.cpp, Speaches, Ollama, LM Studio,
vLLM); others want Speecher to handle it.

Speech-to-text and cleanup have different shapes. Local speech models are
small (0.1 to 2.4 GB), run well on a laptop CPU, and benefit from tight
integration with the Dictation Session. Cleanup models are larger, and there
are mature apps whose whole job is running them.

Research notes behind the numbers here live on the design branch, not in
master.

## Decision

**Local speech runs in-process on transcribe.cpp.** It is MIT licensed, has
one C header, covers Parakeet, Moonshine, Granite, Cohere, Whisper and more,
streams, and is what Handy ships on all three platforms. Speecher pins a
version and wraps it behind `SpeechTranscriber`, because transcribe.cpp is
pre-1.0 and may break its ABI between minor versions.

GPU acceleration is Metal on macOS and Vulkan on Linux and Windows x64, which
covers NVIDIA, AMD and Intel GPUs including integrated ones, with no CUDA
toolkit to ship. Windows on ARM runs on the CPU. CUDA may follow as an
optional download. A GPU picker appears only when a machine has more than one
usable device.

**The first-party model list is short and English-only**, matching the
release's language rule:

| Model | Download | Role |
|---|---|---|
| moonshine-streaming-small | 199 MB | Low-end machines; streams |
| parakeet-unified-en-0.6b | 731 MB | Default; streams |
| granite-speech-5.0-470m-turboctc | 506 MB | Most accurate under 1 GB; only if it emits punctuation |
| cohere-transcribe-03-2026 | 2.4 GB | Best accuracy; dedicated GPUs and Pro/Max Macs |

Parakeet Unified is under the NVIDIA Open Model License, accepted for this
use. Files come from `huggingface.co/handy-computer`, pinned by revision and
checked by sha256, stored in the app data directory, resumable and deletable.
The list is data generated from transcribe.cpp's catalog.

Streaming models are the default. Non-streaming models are labelled "text
appears after you stop". The model loads when a Dictation Session starts,
while pre-roll audio buffers, and unloads after an idle timeout (default 10
minutes).

**Suggestions come from detected hardware, then measured speed.** Speecher
reads GPU devices and memory from transcribe.cpp's device list and system RAM
from the OS, sorts models into tiers, and labels each fits, tight or too
large. Accuracy and download size are real numbers; speed before download is
a coarse label. After download, a short bundled clip is transcribed on the
user's machine and the measured time replaces the label.

**Local refinement uses an external runner.** Speecher does not run cleanup
models itself. It detects Ollama, LM Studio and llama-server on their default
ports (checking each server's own endpoint first, since ports and even
Ollama's API are shared), lists their models, and pulls a suggested model
through Ollama's API with one click. With no runner installed it links to the
Ollama installer. If a bundled runner is ever added, it runs `llama-server` as
a child process, because llama.cpp and transcribe.cpp each vendor their own
ggml and cannot share a process.

**Custom endpoints are one per role.**

- Speech: OpenAI `POST /v1/audio/transcriptions`, multipart 16 kHz WAV, with
  an editable path so whisper.cpp's `/inference` works, and `stream=true` when
  the server accepts it. Set in Settings only.
- Refinement: an OpenAI-compatible (Chat Completions) or Anthropic-compatible
  (Messages) switch, base URL, optional key, and a model picked from
  `GET /v1/models` with free text as fallback. The OpenAI flavour sends
  `reasoning_effort: "none"` and retries without it if rejected. The CLI Proxy
  API server becomes a preset of this endpoint rather than a parallel setting.
  Endpoint keys, including the CLI Proxy key, live in the system keychain.

**Settings and setup.** A new Local models page holds downloads, the hardware
summary, suggestions and the speed test. "Local" and "Custom endpoint" join
the existing provider pickers. The setup assistant no longer requires a sign-in:
it offers local when it finds none and lists it as a choice when it finds one.
The Transcription step gains a Local card that opens model selection and
downloads in the background while setup continues; the Refinement step gains
Local and Custom endpoint cards.

## Consequences

- Speecher works with no subscription and no network after the model
  download.
- Batch speech endpoints need the whole recording after stop, so rule A7 now
  forbids re-sending audio after a provider failure rather than retaining it
  at all. Codex's final re-transcription already worked this way.
- Linux and Windows packages grow by about 20 MB (Vulkan shaders and per-ISA
  CPU backends); macOS by about 2 MB.
- Vulkan builds need `glslc` and Vulkan headers in CI.
- A transcribe.cpp upgrade is a deliberate, tested change, never a floating
  dependency.
- If transcribe.cpp stalls or changes direction, sherpa-onnx (Apache-2.0) is
  the fallback engine. It covers the same English models except Voxtral, but
  has no Vulkan backend, so AMD and Intel GPUs on Linux would run on the CPU.
  Only `LocalSpeechEngine` talks to transcribe.cpp, so a switch stays in one
  class.
- Model files download from the `handy-computer` Hugging Face account, pinned
  by revision and sha256. If those repos move, mirror the pinned files under an
  account the project controls.
- Local speech is off in a default build (`SPEECHER_WITH_LOCAL_SPEECH`) because
  it more than doubles a clean build; release builds turn it on and build every
  CPU backend (`SPEECHER_LOCAL_SPEECH_ALL_CPUS`).
