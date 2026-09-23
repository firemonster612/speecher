# Android client

Kotlin + Jetpack Compose. No React Native, no Qt, no shared code with the desktop app. The desktop `src/providers/` is the protocol spec; port behavior from it and treat drift as a bug. PRD and UX contract: issue #122. The desktop's UI rules in the root `AGENTS.md` do not apply here.

## Rules

- **The keyboard is not a keyboard.** This app ships a dictation-only IME (panel with waveform, live preview, Cancel/Insert) and an accessibility overlay chip. Tapping the chip switches to our IME through the accessibility service's `SoftKeyboardController.switchToInputMethod`; the IME switches back with `InputMethodService.switchInputMethod`. Both are permission-free — no `WRITE_SECURE_SETTINGS`, no adb. Never add a typing layout, and never reintroduce the privileged settings write.
- **Insertion happens once.** Text reaches the target field through `InputConnection.commitText` on Insert, never incrementally while dictating, never via accessibility `ACTION_SET_TEXT`.
- **Tokens live in Keystore-encrypted storage** (`EncryptedSharedPreferences` or equivalent). Never in plaintext files, never logged. Redact tokens in every log line and error message.
- **Errors render inside the panel.** No dialogs, no toasts for dictation failures.
- **Match the C++ before inventing.** For any protocol question (WS framing, headers, query params, token refresh), read the desktop source first: `src/providers/ClaudeVoiceProtocol.cpp`, `CodexDictationClient.cpp`, `CliProxyCredentials.cpp`, and the OAuth facts on issue #123.
- **Build and test**: `./gradlew :app:assembleDebug :app:testDebugUnitTest` from `android/`. Protocol ports carry JUnit tests with literal expected values derived from the C++, never computed by the code under test.
- **UI checks are screenshots you looked at**: emulator screenshot via adb, attached to the PR or report. A green test is not evidence a panel looks right.

## Code quality

Working code that breaks these rules is not done. Reviewers treat a violation as a blocking finding, the same as a bug.

- **`./gradlew check` is the gate.** It runs ktfmt, Android Lint and the compiler with warnings as errors, plus the tests. A change lands green or not at all; fix the warning, never suppress it. The only documented exceptions live in `app/lint.xml`, and every one is scoped to BouncyCastle's `bctls` jar — never to our own source. `TrustAllX509TrustManager` is ignored there because BouncyCastle's own bytecode ships empty-trust-manager classes; our TLS code validates the chain against the system trust store and verifies the hostname, so it is a false positive in a third-party jar. `GradleDependency` and `NewerVersionAvailable` are ignored for `bctls` because it is deliberately pinned at 1.86. Every other dependency is still held current, no other check may be suppressed, and findings in our own source are never exempt.
- **Read `~/.claude/skills/write-less/SKILL.md` before writing.** The diff is the size of the ask. Top-level functions, data classes and sealed types come first; a class exists only when it owns state or a lifecycle (a Service, a ViewModel). One implementation means no interface. Wire dependencies through constructors, with no DI framework and no `Manager`, `Helper` or `Util` names.
- **Use the platform's API.** When Android or AndroidX already does it, call that instead of rebuilding it.
- **Tests cover each behavior once**, with literal expected values. Test code that outgrows the code it covers means cut cases, not add them.
- **Dependencies are current.** Check the latest stable release before adding or bumping one.
