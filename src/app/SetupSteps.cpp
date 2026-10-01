#include "app/SetupSteps.h"

#include "core/AppSettings.h"
#include "core/CliToolDiscovery.h"
#include "core/settings/SettingsSchema.h"

namespace speecher {

QList<SetupStepInfo> setupSteps()
{
    QList<SetupStepInfo> steps{
        {QStringLiteral("welcome"),
         QStringLiteral("Welcome to Speecher"),
         QStringLiteral("Speecher records a short dictation, turns it into text, and sends it to the "
                        "app you were using."),
         QString()},
        {QStringLiteral("transcription"),
         QStringLiteral("Transcription"),
         QStringLiteral("Choose the service Speecher uses to turn speech into a raw transcript."),
         QString()},
        {QStringLiteral("microphone"),
         QStringLiteral("Microphone"),
         QStringLiteral("Choose the input Speecher should record. Speak normally; setup continues "
                        "once the level moves."),
         QString()},
    };
#if defined(Q_OS_MACOS)
    steps.append({QStringLiteral("accessibility"),
                  QStringLiteral("Accessibility"),
                  QStringLiteral("Speecher pastes your dictation into the frontmost app with a "
                                 "synthetic Cmd+V. macOS calls that controlling your computer, so it "
                                 "needs Accessibility permission."),
                  QStringLiteral("Accessibility is off, so Speecher cannot paste your dictation.")});
#elif !defined(Q_OS_WIN)
    steps.append({QStringLiteral("accessibility"),
                  QStringLiteral("Accessibility"),
                  QStringLiteral("Speecher pastes your dictation into the app you are using, and "
                                 "reads the text around your cursor so cleanup understands the "
                                 "context. On Linux both work through the desktop accessibility "
                                 "service (AT-SPI); it also lets Speecher learn your corrections."),
                  QStringLiteral("Accessibility is off, so Speecher cannot paste or read context.")});
#endif
    steps.append({QStringLiteral("delivery"),
                  QStringLiteral("Text delivery"),
#if defined(Q_OS_MACOS)
                  QStringLiteral("Speecher puts the finished text on your clipboard and pastes it "
                                 "into the frontmost app with Cmd+V. The paste needs the "
                                 "Accessibility permission from the previous step; without it the "
                                 "text still reaches your clipboard."),
#elif defined(Q_OS_WIN)
                  QStringLiteral("Speecher puts the finished text on your clipboard and pastes it "
                                 "into the frontmost app with Ctrl+V. Nothing extra needs to be "
                                 "installed."),
#else
                  QStringLiteral("To type for you, Speecher installs a small virtual keyboard. Your "
                                 "computer will ask for your administrator password once; Speecher "
                                 "itself never runs privileged."),
#endif
                  QStringLiteral("The virtual keyboard is not set up, so Speecher cannot type into "
                                 "other apps.")});
    steps.append({QStringLiteral("refinement"),
                  QStringLiteral("Refinement"),
                  QStringLiteral("Refinement can clean up a raw transcript after dictation. Choose a "
                                 "provider, or skip cleanup."),
                  QString()});
    steps.append({QStringLiteral("profiles"),
                  QStringLiteral("Writing profiles"),
                  QStringLiteral("Speecher picks a Writing Profile from the app you dictate into. "
                                 "Choose the fallback profile and how much cleanup and tone "
                                 "adjustment each one gets."),
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
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
    steps.append({QStringLiteral("login"),
                  QStringLiteral("Start at login"),
                  QStringLiteral("Dictation only works while Speecher is running."),
                  QString()});
#endif
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

QString setupWelcomeDetail()
{
    return QStringLiteral("This assistant checks everything dictation needs: your speech service, "
                          "microphone, and how text reaches your apps. It ends by setting up a "
                          "Global Shortcut.");
}

QString setupStepCounter(int step, int total)
{
    return QStringLiteral("Step %1 of %2").arg(step).arg(total);
}

QString setupSignInMissing(bool localOffered, bool cliToolFound)
{
    if (localOffered) {
        return QStringLiteral("No sign-in was found. Sign in, or choose to run on this computer.");
    }
    if (cliToolFound) {
        return QStringLiteral("No ChatGPT, Claude, or CLI Proxy API sign-in was found.");
    }
    return QStringLiteral("This computer can't run a local speech model, and no ChatGPT, Claude, "
                          "or CLI Proxy API sign-in was found. Please get a free ChatGPT or Claude "
                          "account and install Claude Code or Claude Desktop, or Codex.");
}

QString setupWelcomeDeadEnd(bool signInFound, bool localUsable, bool endpointSaved,
                            bool signInProvidersRegistered)
{
    if (signInFound || localUsable || endpointSaved || !signInProvidersRegistered) {
        return {};
    }
    return setupSignInMissing(false,
                              CliToolDiscovery::isClaudeCodeInstalled()
                                  || CliToolDiscovery::isCodexInstalled());
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

QString setupProfilesNote()
{
    return QStringLiteral("The fallback profile is used when Speecher does not recognize the app "
                          "you are dictating into. Every profile can be changed later in Settings.");
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
