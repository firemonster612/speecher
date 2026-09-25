# Android v1 build plan

This is the build contract for the agents implementing v1. The product and UX live in issue #122. Platform facts live in `docs/android/m2-spike-findings.md`, and OAuth facts in the first comment on issue #123. Code rules live in `android/AGENTS.md`. This file covers only how the work is split and what "done" means.

## Split

Two tracks run in parallel in separate worktrees and merge in order.

- **Engine track (GPT-6 Sol):** everything below the UI. It covers sign-in, token storage, audio, both speech clients, refinement, the IME and accessibility services, the silent swap, crash recovery and the self-updater. It owns every file outside `ui/`.
- **UI track (Opus 5.5):** every pixel. It covers the visual identity (colors, type, launcher icon, chip), the dictation panel, onboarding, settings and the home screen. It owns `app/src/main/java/app/speecher/android/ui/` and `app/src/main/res/`. The user rejected the scaffold's look outright, so this track starts from zero. It is not a polish pass.

The tracks meet at `app/src/main/java/app/speecher/android/dictation/DictationState.kt`, which is already committed. The engine produces those types, and the UI renders them. Either track may add a field or case there. Neither may change the meaning of an existing one without telling the orchestrator.

## Engine track: done means

1. **Sign-in, both providers.** Authorization code with PKCE in a Custom Tab, with the redirect caught by a loopback listener on the vendor's fixed port (Anthropic 54545 `/callback`, OpenAI 1455 `/auth/callback`). Anthropic also offers a paste-the-code fallback (`code#state`). Token exchange uses a JSON body for Anthropic and a form body for OpenAI. Refresh follows `src/providers/CliProxyCredentials.cpp`. Tokens are stored encrypted with Keystore. `Provider` sign-in state is exposed for `SetupStatus.signedIn`.
2. **Speech clients.** Claude voice WebSocket and Codex dictation WebSocket, ported from `src/providers/ClaudeVoiceClient.cpp` and `CodexDictationClient.cpp` into `:protocol`, pure JVM with OkHttp, with JUnit tests against a fake server. Audio is AudioRecord `VOICE_RECOGNITION` at 16 kHz mono PCM16.
3. **Refinement.** Ported from `src/providers/AnthropicApiRefiner.cpp`, `OpenAiRefiner.cpp` and `TranscriptRefinementPrompt.cpp`. Default models match the desktop defaults in `src/core/settings/`.
4. **Dictation engine.** One engine drives `DictationState`. Its commands are start, stop, cancel, insert and insert-refined, and it exposes a level meter from the audio. The IME commits text through `InputConnection.commitText` exactly once, on insert.
5. **Services.** The IME overrides `onEvaluateInputViewShown()`. The manifest has `<queries>` for `android.view.InputMethod`. The accessibility service runs the chip overlay with `fitInsetsTypes = 0` and cutout mode ALWAYS. The chip is hidden while our IME is active and in password fields. The IME must never be enabled by rewriting `ENABLED_INPUT_METHODS`.
6. **Swap.** Tapping the chip saves the current IME and subtype, then writes `DEFAULT_INPUT_METHOD`. The engine starts connecting on the tap, not when the panel appears (see the flicker finding). Switching back uses `switchInputMethod` with the saved subtype. If the process restarts while our IME is the default, the saved IME is restored.
7. **Self-updater.** Checks GitHub releases for a newer APK, then installs it through PackageInstaller.
8. **Tests.** Every protocol path and the engine state machine are unit-tested against fakes. `./gradlew check` is green.

## UI track: done means

1. **Visual identity.** One coherent look taken from the desktop brand: `packaging/io.github.firemonster612.speecher.svg`, a warm off-white pill with four waveform bars on near-black. Material 3 with a custom color scheme, light and dark. A new adaptive launcher icon with a monochrome layer. The chip is a small, quiet, docked control that looks native beside Gboard. It carries no text and does not shout.
2. **Dictation panel.** Renders every `DictationState`: connecting, listening (live waveform and a streaming preview in the panel), refining, and failed (an in-panel recovery action per `FailureReason`, never a dialog). Buttons: Cancel, Insert, and Insert refined, which appears only when `SpeecherSettings.refinementEnabled`. Same height as a keyboard.
3. **Onboarding.** A checklist driven by `SetupStatus`: sign in to Claude and ChatGPT, microphone permission (requested here, since the IME cannot ask), enable the keyboard, enable the chip service, and the one-time adb grant shown as a copyable command with live verification. It ends with a practice field.
4. **Settings.** Transcription provider, refinement toggle and provider, vocabulary list and accounts (sign out, sign in again).
5. **Previews and screenshots.** Every screen and every panel state has a Compose `@Preview` fed by fake state. Done means the orchestrator has looked at emulator screenshots of each one, in light and dark.

## Verification limits

The orchestrator cannot sign in to the user's accounts. The engine track proves its speech clients against a local fake WebSocket server. Real sign-in and live dictation are the user's first test. These still need the real phone: floating, split and OEM keyboards, the restricted-settings step for sideloaded accessibility services, and flicker on real hardware.
