#pragma once

#include <QList>
#include <QString>

#include <optional>

namespace speecher {

enum class ShortcutActivationMode;
struct SettingsRow;

// One step of the setup assistant, as every front end shows it. Which steps
// hold Continue is each front end's, since the state behind a gate is.
struct SetupStepInfo {
    QString id;
    QString title;
    // The lead under the title. Empty for "ready", whose lead follows the flow.
    QString intro;
    // Why the step is unfinished, where that does not depend on state; the
    // functions below word the rest.
    QString blocked;
};

// The steps this build's assistant walks, in order. Accessibility is not a
// step on Windows, and Text delivery is one only on Linux, where it installs
// the virtual keyboard. Ready is last.
QList<SetupStepInfo> setupSteps();
// Null for a step this platform does not have.
const SetupStepInfo *findSetupStep(const QString &id);

// The assistant window's title.
QString setupWindowTitle();
// "Step 2 of 9".
QString setupStepCounter(int step, int total);

// The Transcription step's dead-end note, or empty while any way into
// dictation remains: a found sign-in, a machine that can run a model, a
// configured speech server, or no sign-in providers to wait for. While it
// shows, it is the step's only verdict and the step holds Continue. Core also
// decides the wording from whether Claude Code or Codex is installed:
// installed means signing in is the missing step, otherwise the note says to
// get an account and install one.
QString setupTranscriptionDeadEnd(bool signInFound, bool localUsable, bool endpointSaved,
                                  bool signInProvidersRegistered);
// Why a state-dependent gate is shut. An empty label means no transcription
// service is available.
QString setupTranscriptionBlocked(bool localSelected, const QString &providerLabel);
enum class SetupMicrophoneProblem { NoDevice, NoAccess, Silent };
QString setupMicrophoneBlocked(SetupMicrophoneProblem problem);

// Shown under the level meter once it has heard nothing for this long.
inline constexpr int kSetupSilentMicrophoneMs = 5000;
QString setupSilentMicrophoneHint();

// A settings row as the Settings window labels it, so a setup control named
// after one reads the same in both places.
const SettingsRow &setupSchemaRow(const QString &rowId);

// "To dictate, hold F13 while you speak.", following the chosen mode.
QString setupActivationInstruction(ShortcutActivationMode mode, const QString &shortcut);

// A provider row's verdict: Ready, Not signed in for a sign-in, and Not set up
// for someone's own model or server.
QString setupProviderVerdict(const QString &providerId, bool ready);
// The Ready step's refinement row: No cleanup for "none", Not checked while
// no probe has answered, otherwise the provider's verdict.
QString setupRefinementStatus(const QString &providerId, std::optional<bool> ready);

// One line of the Ready step's checklist: "Microphone — Built-in".
QString setupChecklistLine(const QString &stepId, const QString &choice);
// The verdict on that line for pasting, which is not a gate on macOS: Ready,
// or Clipboard only while Accessibility is off.
QString setupPasteVerdict(bool pastes);

// The fixed wording every assistant shares: buttons, card titles, and the
// lines the transcription, microphone and refinement steps show.
enum class SetupText {
    SkipSetup,
    CheckAgain,
    GoToStep,
    HowToDictate,
    TranscriptionService,
    LocalSpeechNote,
    DownloadToContinue,
    DownloadContinues,
    CloseWhileDownloading,
    CliproxyAccount,
    ListeningForInput,
    InputDetected,
    UsesYourSignIn,
    YourOwnModels,
    CleanupProvider,
    SkipCleanup,
    LookingForRunners,
    NoRunnerFound,
    NoRunner,
    RawUntilRunner,
    InstallRunner,
    GetOllama,
    DownloadWithOllama,
    EndpointModelHint,
};
QString setupText(SetupText text);

// "Claude Voice is ready."
QString setupProviderReady(const QString &providerLabel);
// A refinement provider whose sign-in failed, which is a warning, not a gate.
QString setupRefinementNotSignedIn(const QString &providerLabel);
// What the Ready checklist names for a speech choice: "Moonshine Small, on
// this computer", or "Claude Voice (CLI Proxy API)" for a CLI Proxy API sign-in.
QString setupLocalSpeechChoice(const QString &modelName);
QString setupCliproxySpeechChoice(const QString &providerLabel);

// The Ready step: its lead, and the heading and footer around the steps
// still holding Finish.
QString setupReadyIntro(bool blocked, bool downloading);
QString setupBlockedHeading();
QString setupBlockedFooter();

} // namespace speecher
