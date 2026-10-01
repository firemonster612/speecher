#include "app/SetupSteps.h"

#include "core/AppSettings.h"
#include "core/settings/SettingsSchema.h"

namespace speecher {

QList<SetupStepInfo> setupSteps()
{
    QList<SetupStepInfo> steps{
        {QStringLiteral("welcome"),
         QStringLiteral("Welcome"),
         QStringLiteral("Speecher records a short dictation, turns it into text and sends it to the app "
                        "you were using. This assistant checks everything dictation needs: your speech "
                        "service, microphone, how text reaches your apps and a Global Shortcut."),
         QString()},
        {QStringLiteral("transcription"),
         QStringLiteral("Transcription"),
         QStringLiteral("Choose the service that turns your speech into text."),
         QString()},
        {QStringLiteral("microphone"),
         QStringLiteral("Microphone"),
         QStringLiteral("Choose the microphone Speecher should record, then speak to check that the "
                        "level moves."),
         QString()},
    };
#if defined(Q_OS_MACOS)
    steps.append({QStringLiteral("accessibility"),
                  QStringLiteral("Accessibility"),
                  QStringLiteral("Speecher pastes your dictation into the frontmost app with a "
                                 "synthetic Cmd+V. macOS calls that controlling your computer, so it "
                                 "needs Accessibility permission. Without it, dictation still reaches "
                                 "your clipboard."),
                  QStringLiteral("Accessibility is off, so dictation only reaches your clipboard.")});
#elif !defined(Q_OS_WIN)
    steps.append({QStringLiteral("accessibility"),
                  QStringLiteral("Accessibility"),
                  QStringLiteral("Speecher pastes your dictation into the app you are using, and "
                                 "reads the text around your cursor so cleanup understands the "
                                 "context. On Linux both work through the desktop accessibility "
                                 "service (AT-SPI); it also lets Speecher learn your corrections."),
                  QStringLiteral("Accessibility is off, so Speecher cannot paste or read context.")});
    steps.append({QStringLiteral("delivery"),
                  QStringLiteral("Text delivery"),
                  QStringLiteral("To type for you, Speecher installs a small virtual keyboard. Your "
                                 "computer will ask for your administrator password once; Speecher "
                                 "itself never runs privileged."),
                  QStringLiteral("The virtual keyboard is not set up, so Speecher cannot type into "
                                 "other apps.")});
#endif
    steps.append({QStringLiteral("refinement"),
                  QStringLiteral("Refinement"),
                  QStringLiteral("Refinement can clean up your dictation before it is delivered. Choose "
                                 "a provider, or skip cleanup. Writing Profiles adjust cleanup for each "
                                 "app; change them later on the %1 page.")
                      .arg(paneTitleForRow(QStringLiteral("defaultWritingProfile"))),
                  QString()});
    steps.append({QStringLiteral("shortcut"),
                  QStringLiteral("Global Shortcut"),
                  QStringLiteral("Choose what starts dictation: a key combination, or one key on its "
                                 "own, such as Right %1. Then choose what pressing it does.")
#if defined(Q_OS_MACOS)
                      .arg(QStringLiteral("Option")),
#else
                      .arg(QStringLiteral("Alt")),
#endif
                  QStringLiteral("No shortcut is set, so there is no way to start dictating.")});
    steps.append({QStringLiteral("ready"), QStringLiteral("Ready to dictate"), QString(), QString()});
    return steps;
}

const SetupStepInfo *findSetupStep(const QString &id)
{
    static const QList<SetupStepInfo> steps = setupSteps();
    for (const SetupStepInfo &step : steps) {
        if (step.id == id) {
            return &step;
        }
    }
    return nullptr;
}

QString setupWindowTitle()
{
    return QStringLiteral("Speecher Setup Assistant");
}

QString setupStepCounter(int step, int total)
{
    return QStringLiteral("Step %1 of %2").arg(step).arg(total);
}

QString setupTranscriptionBlocked(bool localSelected, const QString &providerLabel)
{
    if (localSelected) {
        return QStringLiteral("Download a speech model to continue.");
    }
    if (providerLabel.isEmpty()) {
        return QStringLiteral("No transcription service is available.");
    }
    return QStringLiteral("%1 is not signed in.").arg(providerLabel);
}

QString setupMicrophoneBlocked(SetupMicrophoneProblem problem)
{
    switch (problem) {
    case SetupMicrophoneProblem::NoDevice:
        return QStringLiteral("No microphone was found.");
    case SetupMicrophoneProblem::NoAccess:
        return QStringLiteral("Microphone access is off.");
    case SetupMicrophoneProblem::Silent:
        break;
    }
    return QStringLiteral("No microphone input has been detected.");
}

QString setupSilentMicrophoneHint()
{
    return QStringLiteral("No input yet \u2014 check that the microphone isn't muted, or pick another device.");
}

const SettingsRow &setupSchemaRow(const QString &rowId)
{
    // Labels do not depend on the context beyond what it enables, so one
    // built with everything enabled answers for every build.
    static const SettingsSchema schema = [] {
        SchemaContext context;
        context.speechProviders = {{QStringLiteral("local"), QString()}};
        context.virtualKeyboardSetup = true;
        return buildSettingsSchema(context);
    }();
    const SettingsRow *row = schema.row(rowId);
    if (!row) {
        qFatal("setup names settings row %s, which the schema does not have", qPrintable(rowId));
    }
    return *row;
}

QString setupActivationInstruction(ShortcutActivationMode mode, const QString &shortcut)
{
    switch (mode) {
    case ShortcutActivationMode::PushToTalk:
        return QStringLiteral("To dictate, hold %1 while you speak.").arg(shortcut);
    case ShortcutActivationMode::Hybrid:
        return QStringLiteral("To dictate, tap %1 to toggle, or hold it to dictate until release.")
            .arg(shortcut);
    case ShortcutActivationMode::Toggle:
        break;
    }
    return QStringLiteral("To dictate, press %1 to start, press it again to stop.").arg(shortcut);
}

QString setupProviderVerdict(const QString &providerId, bool ready)
{
    if (ready) {
        return QStringLiteral("Ready");
    }
    return providerId == QStringLiteral("local") || providerId == QStringLiteral("endpoint")
        ? QStringLiteral("Not set up")
        : QStringLiteral("Not signed in");
}

QString setupRefinementStatus(const QString &providerId, std::optional<bool> ready)
{
    if (providerId == QStringLiteral("none")) {
        return QStringLiteral("No cleanup");
    }
    return ready ? setupProviderVerdict(providerId, *ready) : QStringLiteral("Not checked");
}

QString setupChecklistLine(const QString &stepId, const QString &choice)
{
    const SetupStepInfo *step = findSetupStep(stepId);
    return QStringLiteral("%1 \u2014 %2").arg(step ? step->title : stepId, choice);
}

QString setupPasteVerdict(bool pastes)
{
    return pastes ? setupProviderVerdict(QString(), true) : QStringLiteral("Clipboard only");
}

QString setupText(SetupText text)
{
    switch (text) {
    case SetupText::SkipSetup:
        return QStringLiteral("Skip setup");
    case SetupText::CheckAgain:
        return QStringLiteral("Check again");
    case SetupText::GoToStep:
        return QStringLiteral("Go to step");
    case SetupText::HowToDictate:
        return QStringLiteral("How to dictate");
    case SetupText::TranscriptionService:
        return QStringLiteral("Transcription service");
    case SetupText::LocalSpeechNote:
        return QStringLiteral("Runs on this computer. No account, works offline.");
    case SetupText::DownloadToContinue:
        return QStringLiteral("Download a model to continue. It keeps going while you finish setup.");
    case SetupText::DownloadContinues:
        return QStringLiteral("The download keeps going while you finish setup.");
    case SetupText::CloseWhileDownloading:
        return QStringLiteral("You can close this window. The download keeps going, and Speecher shows a "
                              "notification when you can start dictating.");
    case SetupText::CliproxyAccount:
        return QStringLiteral("CLI Proxy API account");
    case SetupText::ListeningForInput:
        return QStringLiteral("Listening for microphone input\u2026");
    case SetupText::InputDetected:
        return QStringLiteral("Microphone input detected.");
    case SetupText::UsesYourSignIn:
        return QStringLiteral("Uses your sign-in");
    case SetupText::YourOwnModels:
        return QStringLiteral("Your own models");
    case SetupText::CleanupProvider:
        return QStringLiteral("Cleanup provider");
    case SetupText::SkipCleanup:
        return QStringLiteral("Skip cleanup and deliver the raw transcript");
    case SetupText::LookingForRunners:
        return lookingForRunnersStatus();
    case SetupText::NoRunnerFound:
        return QStringLiteral("No Local Runner found on this computer.");
    case SetupText::NoRunner:
        return QStringLiteral("No runner");
    case SetupText::RawUntilRunner:
        return QStringLiteral("Until a runner is set up, dictation delivers the raw transcript.");
    case SetupText::InstallRunner:
        return QStringLiteral("Cleanup models run in a separate app. Install Ollama, then choose Check again "
                              "and Speecher will set up a model through it. LM Studio and llama-server work "
                              "too.");
    case SetupText::GetOllama:
        return QStringLiteral("Get Ollama");
    case SetupText::DownloadWithOllama:
        return QStringLiteral("Download with Ollama");
    case SetupText::EndpointModelHint:
        return QStringLiteral("Connect to list the server's models, or type one.");
    }
    return {};
}

QString setupProviderReady(const QString &providerLabel)
{
    return QStringLiteral("%1 is ready.").arg(providerLabel);
}

QString setupRefinementNotSignedIn(const QString &providerLabel)
{
    return QStringLiteral("%1 is not signed in. Dictation will deliver the raw transcript.").arg(providerLabel);
}

QString setupLocalSpeechChoice(const QString &modelName)
{
    return QStringLiteral("%1, on this computer").arg(modelName);
}

QString setupCliproxySpeechChoice(const QString &providerLabel)
{
    return QStringLiteral("%1 (CLI Proxy API)").arg(providerLabel);
}

QString setupReadyIntro(bool blocked, bool downloading)
{
    if (blocked) {
        return QStringLiteral("Speecher can't dictate yet. Finish the steps below, or go back and "
                              "change your choices.");
    }
    return downloading ? QStringLiteral("Setup is complete except for the speech model download.")
                       : QStringLiteral("Setup is complete.");
}

QString setupBlockedHeading()
{
    return QStringLiteral("A few steps still need attention:");
}

QString setupBlockedFooter()
{
    return QStringLiteral("Finish becomes available once every step above is resolved.");
}

} // namespace speecher
