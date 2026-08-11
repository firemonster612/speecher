# KDE Flow-Parity TODO

This is the authoritative implementation and release checklist.

This document records the complete feature set agreed for the first Speecher
release inspired by Wispr Flow. It replaces the planning language in GitHub
issue #2 as the definition of “done.” A checked box requires both implementation
and the evidence named here; code that only covers a favorable path is not
complete.

## Product boundary

- Speecher is a local-first, open-source desktop application. It has no Speecher
  account, subscription, billing, team, enterprise, or cloud-sync system.
- Plasma 6 on Wayland is the supported and tested desktop for this release.
- Platform behavior lives behind replaceable interfaces so later KDE, GNOME,
  other Linux desktop, Windows, and macOS adapters do not require rewriting the
  Dictation Session.
- English is the only selectable speech language in this release. The provider
  interface must be capable of receiving a language in a later release.
- The existing daemon is the single source of runtime state for the desktop UI
  and CLI.

## Required features and acceptance criteria

### A. Activation and Dictation Session

- [x] **A1 — Toggle activation.** `speecher toggle` starts an idle Dictation
  Session and stops a listening one through the existing daemon. Invoking it
  when the desktop window is closed starts the background app.
- [x] **A2 — Push to talk.** `speecher start` and `speecher stop` are
  idempotent daemon commands suitable for separate Plasma press and release
  bindings. Speecher does not own a cross-desktop shortcut manager.
- [x] **A3 — Central state.** CLI status, the popup, and the settings/main
  window reflect the same daemon-owned Dictation Session state.
- [x] **A4 — Retained commands only.** The CLI exposes toggle, start, stop,
  status, show settings, and the plain/HTML per-session overrides. It does not
  add copy-last, paste-last, transcript undo, Polish, notes, diff, command, or
  meeting shortcuts.
- [x] **A5 — Original Target.** The Target is captured when the session starts
  and remains the intended insertion target even if the non-activating popup is
  shown. Delivery must not blindly paste into an unrelated control.
- [x] **A6 — Attempt lifecycle.** A speech attempt has explicit start,
  finish-input, completion, typed failure, and cancellation behavior. Final
  processing waits for authoritative completion rather than a fixed delay.
- [x] **A7 — No full-audio replay.** Speecher streams each captured PCM chunk
  once and does not retain or resend the complete recording after stop. Provider
  failures surface promptly instead of starting a slow second transcription.
- [x] **A8 — Session isolation.** Settings and a CLI output override are
  snapshotted when recording starts. Starting, cancelling, or completing a
  session cannot leak transcript, screenshot, audio, or provider events into
  another session.

### B. Speech and transcript stages

- [x] **B1 — Claude subscription speech.** The default speech adapter uses the
  user’s existing Claude Code/Claude subscription OAuth session and Claude
  Voice’s Deepgram Nova stream. It does not require a separate Deepgram account
  or key.
- [x] **B2 — Replaceable provider.** Claude-specific transport, credentials,
  event schema, and keyterms remain inside a speech-provider adapter.
- [x] **B3 — English stream.** Audio is captured in the format required by the
  English Claude Voice stream, with selected-microphone and system-default
  fallback behavior.
- [x] **B4 — Live Raw Transcript.** While listening, interim and endpoint
  revisions update the popup as a replaceable working Raw Transcript.
- [x] **B5 — Frozen Raw Transcript.** After provider completion, the final Raw
  Transcript remains visible while optional refinement and delivery run.
- [x] **B6 — Optional refinement.** The user may disable refinement or choose
  Anthropic or OpenAI. Cleanup has None, Light, Medium, and High strengths and
  covers punctuation, capitalization, filler removal, self-correction, and
  useful structure within the selected strength. A refinement failure visibly
  falls back to the final Raw Transcript and never loses the dictation.
- [x] **B7 — Raw and refined separation.** Refinement input and output cannot
  mutate or discard the frozen Raw Transcript.
- [x] **B8 — Compact popup feedback.** The active popup contains one line with
  only the configurable trailing Raw Transcript words, seven by default, and
  the small waveform/progress animation above it. Older words roll off the left.
  It does not show status, language, Target, or WPM rows. Silent audio, invalid
  input, capture interruption, and microphone changes still produce an
  actionable message rather than hanging or silently delivering an empty
  result.
- [x] **B9 — Selection editing.** When a non-secure editable Target has selected
  text, the Raw Transcript is treated as spoken editing instructions. The
  refinement model receives the complete selection plus the inferred Writing
  Profile, tone, vocabulary, and normal refinement rules, and returns only the
  complete revised selection. Delivery replaces the saved selection when the
  configured Paste Rule permits it, always copies the result first, and leaves
  the original selection untouched when refinement fails.

### C. Target context and Writing Profiles

- [x] **C1 — Bounded Target context.** When exposed by the target application,
  Speecher captures application identity/type, window title, document URL,
  control identity and role, caret, selection, and a bounded amount of text
  before and after the caret.
- [x] **C2 — Useful editor context.** For supported Plasma editors, capture the
  useful filename, editor text, nearby variables/identifiers, editor identity,
  and coding-terminal/CLI context the application exposes without reading an
  entire document. Richer IDE semantics stay behind a future replaceable
  platform adapter and are not fabricated from inaccessible data.
- [x] **C3 — Context-aware refinement.** Target context is an explicit setting.
  When enabled, refiners receive bounded, clearly marked untrusted context and
  may use it for references, terminology, and local writing style without
  treating target text as instructions.
- [x] **C4 — Automatic Writing Profile.** Speecher infers the Writing Profile
  with the precedence specific-application override, detected application
  category, then configurable fallback. Work, Email, Personal, and Other are
  supported; the user does not choose one for every Dictation Session. Each
  profile has a cleanup strength and optional tone. Tone is an explicit setting,
  not an automatically learned personal style. The inferred profile and its
  settings are sent to refinement.
- [x] **C5 — Optional screenshot/OCR context.** Screenshot context is off by
  default, requested through the Plasma portal, sent only to a selected
  image-capable refiner, and retained only in memory for the active session.
  Cancellation, failure, or completion clears it. Text-only refiners do not
  trigger a capture.
- [x] **C6 — Context privacy boundary.** Secure controls expose no nearby text,
  selection, screenshot context, direct insertion, paste attempt, or correction
  observation.

### D. Output representation and delivery

- [x] **D1 — Plain and HTML output.** Plain text and sanitized HTML are
  selectable defaults. HTML delivery always includes an authoritative plain
  fallback and never sends markup through a plain-text typing backend.
- [x] **D2 — Per-session format commands.** `toggle` and `start` accept
  `--format plain` and `--format html` without changing the saved default, so
  Plasma shortcuts can bind the formats separately.
- [x] **D3 — Paste Rule precedence.** Delivery chooses a specific-application
  rule before an application-category rule and a global rule last.
- [x] **D4 — Paste methods.** A Paste Rule can choose standard paste,
  terminal paste, or clipboard-only. Users can set defaults for categories
  such as terminals and override a specific application. An explicit global
  paste rule still applies to the currently focused control when desktop
  accessibility cannot identify the application.
- [x] **D5 — Always copy.** Every successful transcription is put on the
  clipboard before insertion is attempted. If insertion is unavailable,
  refused, or fails, the result remains available for manual paste and the
  popup reports that outcome.
- [x] **D6 — Optional clipboard restoration.** When enabled, the previous
  multi-MIME clipboard contents are restored after successful virtual-keyboard
  input or verified direct insertion. They are not restored after failed input.
- [x] **D7 — Original-target safety.** When the desktop identifies the captured
  Target, normal paste is attempted only while it remains focused. If no Target
  identity is available, only an explicit global paste rule may send input to
  the current focus. Direct accessibility insertion into an unfocused saved
  Target is allowed only when its identity and bounded text fingerprint still
  match.
- [x] **D8 — Honest verification.** Results distinguish Copied, Input Sent,
  Accepted by Target, and Verified in Target. Synthetic input is never reported
  as verified without target readback.
- [x] **D9 — Secure refusal.** Speecher never forces insertion into password,
  privileged, or otherwise secure targets.
- [x] **D10 — No chunking.** Long output follows the same single delivery path;
  Speecher does not split it into artificial chunks.

### E. Local vocabulary and correction learning

- [x] **E1 — Vocabulary Entries.** Users can add, edit, and delete local words
  and phrases that are supplied as speech keyterms and refinement vocabulary.
  Entries retain source, star, frequency, and last-used metadata; the UI
  deduplicates and sorts entries, imports CSV, and supports bulk deletion.
- [x] **E2 — Replacements and Snippets.** Users can manage spoken phrase
  replacements and longer Snippet expansions locally. Snippets support JSON
  import.
- [x] **E3 — Protected replacements.** Replacement results survive optional
  refinement without exposing internal placeholders or allowing the model to
  corrupt the expansion.
- [x] **E4 — Correction observation.** After verified insertion into a target
  with reliable bounded readback, Speecher briefly observes a user edit and
  creates a Learned Correction only when stable anchors identify one
  high-confidence replacement. It does not learn from ambiguous, secure,
  unverified, or inaccessible targets.
- [x] **E5 — Local persistence and use.** Learned Corrections are stored
  locally, source-marked, included in future vocabulary/replacements when
  enabled, and never uploaded as a correction-history dataset.
- [x] **E6 — Management and undo.** The settings UI allows Learned Corrections
  to be reviewed, edited, enabled, disabled, deleted, and easily undone after
  an accidental automatic learn or deletion.

### F. Native desktop UI and feedback

- [x] **F1 — Native settings shell.** Settings use a KDE System Settings-style
  category sidebar, one page at a time, conventional footer actions, muted
  section headings, and rounded divided setting cards.
- [x] **F2 — Host-native controls.** The production UI uses standard Qt
  controls so Breeze supplies Plasma appearance and another supported host can
  supply its native widget style later.
- [x] **F3 — Popup.** A non-activating bottom-center popup shows listening,
  processing, Raw Transcript, completion/fallback, and error states without
  stealing focus from the Target.
- [x] **F4 — Sounds.** Start and stop feedback sounds are optional.
- [x] **F5 — Microphone settings.** Users can select an input device. If it is
  unavailable, Speecher tries the system default and reports a failure when no
  usable device exists.
- [x] **F6 — Manual updates.** The UI may display the installed version and a
  manual project/download link. Speecher performs no automatic update check or
  unattended update.

## Required Plasma 6 Wayland release evidence

The release matrix is Kate, Konsole, Firefox, Helium, Thunderbird, LibreOffice
Writer, T3 Code, and a Qt password-field fixture. For every installed entry,
record:

1. CLI toggle and press/release activation behavior.
2. Whether the popup preserves Target focus.
3. Captured Target identity, category, Writing Profile, caret, selection, and
   bounded context.
4. Plain and HTML clipboard representations.
5. selected specific/category/global Paste Rule and attempted method.
6. the strongest honest delivery receipt.
7. clipboard restoration behavior.
8. correction-observation capability and result.

The Qt password fixture must prove that context, screenshot, insertion,
verification, and correction learning are all refused. Missing matrix
applications must be installed for the release check; “not installed” is not a
passing result.

The live provider check must use an authorized Claude subscription and retain no
credential, transcript, or audio contents. It records only client version/hash,
frame types and keys, byte counts, timing, close/error classification, and
keyterm transport. It must cover short speech, immediate stop, pause-delimited
speech, finalization, and authentication refusal.

## Release evidence

- Plasma host: `cachy`, Plasma 6 Wayland, 2026-07-30.
- Automated build and test: `cmake --build build -j2` followed by
  `ctest --test-dir build --output-on-failure`; all tests passed.
- The automated suite includes state/IPC isolation, toggle and idempotent
  press/release commands, output-format snapshots, provider completion,
  bounded context/privacy, refinement fallback, protected replacements,
  vocabulary persistence, delivery safety,
  multi-MIME clipboard restoration, and correction-learning gates.
- Local WebSocket integration checks cover immediate finalization,
  pause-delimited endpoints, authentication refusal, and keyterm/query shape
  through the production Claude Voice client.
- The live application results and honest receipts are recorded in
  `docs/research/0003-plasma-application-matrix.md`. Every required application
  was installed and exercised; the secure fixture additionally passed its live
  clipboard-only refusal check.
- Live audio capture used a deliberately missing saved device ID, fell back to
  Cachy's default PipeWire input, and produced mono 16 kHz signed 16-bit PCM
  without retaining audio.
- The native Wayland clipboard publish/restore check passed 20 consecutive
  multi-MIME cycles after the final ownership-handoff build.
- The settings shell was inspected under Breeze in the live Plasma session and
  accepted. Closing Settings left the rebuilt background daemon running.
- The external provider release check passed with a fresh Claude subscription
  OAuth credential on `cachy`: the production Claude Voice WebSocket selected
  Deepgram Nova 3, accepted real-time mono 16 kHz signed 16-bit PCM, produced
  interim revisions and a finalized endpoint, and completed an immediate-stop
  cycle. Test output retained only event types, key names, and transcript
  lengths; the disposable audio was deleted immediately afterward.

## Explicitly excluded

- Internal cross-desktop global-shortcut management, stuck-key watchdogs, and
  arbitrary computer control.
- Non-English UI, language detection, language packs, and wrong-language help.
- Saved rewrite, Polish, Transform, and Instruct tools.
- Transcript/audio history, notes, Scratchpad, meetings, calendar, tasks,
  connectors, insights, focus mode, and notification systems.
- Accounts, cloud sync, teams, subscriptions, billing, referrals, enterprise
  policy, and managed deployment.
- Team dictionaries and shared vocabulary.
- Automatic tone matching, writing-sample learning, voice-preference learning,
  personal-style profiles, email signatures, and signature links.
- Specialized BLE, Jabra, mouse, microphone-holder, and other hardware systems.
- Extensions, MCP, terminal agents, local Claude CLI bridges, and IDE plugins in
  this release.
- Long-text chunking, analytics/telemetry, automatic updates, and alternate
  experimental speech deployments.
