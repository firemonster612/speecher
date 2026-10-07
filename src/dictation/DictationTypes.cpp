#include "dictation/DictationTypes.h"

namespace speecher {

QString dictationStateName(DictationState state)
{
    switch (state) {
    case DictationState::Idle: return QStringLiteral("idle");
    case DictationState::Starting: return QStringLiteral("starting");
    case DictationState::Listening: return QStringLiteral("listening");
    case DictationState::Paused: return QStringLiteral("paused");
    case DictationState::Stopping: return QStringLiteral("stopping");
    case DictationState::Refining: return QStringLiteral("refining");
    case DictationState::Delivering: return QStringLiteral("delivering");
    case DictationState::Error: return QStringLiteral("error");
    case DictationState::Reviewing: return QStringLiteral("reviewing");
    }
    return QStringLiteral("error");
}

DictationToggleAction dictationToggleAction(const QString &stateName)
{
    const QString lowered = stateName.toLower();
    if (lowered == QStringLiteral("starting") || lowered == QStringLiteral("listening")
        || lowered == QStringLiteral("paused")) {
        return {QStringLiteral("Stop dictation"), true};
    }
    if (lowered == QStringLiteral("refining")) {
        return {QStringLiteral("Cancel refinement"), true};
    }
    // Nothing to do while the text is on its way, so the control says why.
    if (lowered == QStringLiteral("stopping") || lowered == QStringLiteral("delivering")
        || lowered == QStringLiteral("reviewing")) {
        return {dictationStatusLabel(lowered), false};
    }
    // Keyed on the state name because that is what the callers receive over
    // their state-change signals, so the compiler cannot enforce coverage:
    // an unrecognised or future state name deliberately falls through to an
    // enabled Start, which is also what idle and error present.
    return {QStringLiteral("Start dictation"), true};
}

QString dictationStatusLabel(const QString &stateName, const QString &message)
{
    const QString lowered = stateName.toLower();
    if (lowered == QStringLiteral("starting") || lowered == QStringLiteral("listening")) {
        return QStringLiteral("Listening…");
    }
    if (lowered == QStringLiteral("paused")) {
        return QStringLiteral("Paused");
    }
    if (lowered == QStringLiteral("stopping")) {
        return QStringLiteral("Transcribing…");
    }
    if (lowered == QStringLiteral("refining")) {
        return QStringLiteral("Refining…");
    }
    if (lowered == QStringLiteral("delivering")) {
        return message.isEmpty() ? QStringLiteral("Delivering…") : message;
    }
    if (lowered == QStringLiteral("reviewing")) {
        return QStringLiteral("Review the edit in the popup");
    }
    if (lowered == QStringLiteral("error")) {
        return message.isEmpty() ? QStringLiteral("Dictation failed") : message;
    }
    return QStringLiteral("Idle");
}

QString dictationFailureNote(const QString &stateName, const QString &lastFailure)
{
    return stateName.toLower() == QStringLiteral("error") ? QString() : lastFailure;
}

QString dictationShortcutHint(const QString &shortcut)
{
    return shortcut.isEmpty()
        ? QStringLiteral("Set a Global Shortcut to dictate from anywhere.")
        : QStringLiteral("Press %1 anywhere to dictate into the app you're using.").arg(shortcut);
}

bool dictationListeningPresentation(const QString &stateName)
{
    const QString lowered = stateName.toLower();
    return lowered == QStringLiteral("starting") || lowered == QStringLiteral("listening");
}

bool dictationCancelable(const QString &stateName)
{
    const QString lowered = stateName.toLower();
    return lowered == QStringLiteral("starting") || lowered == QStringLiteral("listening")
        || lowered == QStringLiteral("paused") || lowered == QStringLiteral("stopping")
        || lowered == QStringLiteral("refining") || lowered == QStringLiteral("reviewing");
}

bool dictationPausable(const QString &stateName)
{
    const QString lowered = stateName.toLower();
    return lowered == QStringLiteral("listening") || lowered == QStringLiteral("paused");
}

SessionKeys sessionKeysWanted(const QString &stateName,
                              bool escapeCancelsDictation,
                              const ShortcutBinding &cancelShortcut,
                              const ShortcutBinding &pauseShortcut,
                              bool suspended)
{
    if (suspended) {
        return {};
    }
    const ShortcutBinding escape{QKeySequence(Qt::Key_Escape)};
    const bool reviewing = stateName.toLower() == QStringLiteral("reviewing");
    const bool cancels = dictationCancelable(stateName) && escapeCancelsDictation && pauseShortcut != escape;
    return {cancelShortcut != escape && (reviewing || cancels), reviewing};
}

QString cancelDictationCaption()
{
    return QStringLiteral("Cancel dictation");
}

SessionControls sessionControls(const QString &stateName)
{
    const QString lowered = stateName.toLower();
    const bool paused = lowered == QStringLiteral("paused");
    const bool listening = lowered == QStringLiteral("listening");
    const bool pauseVisible = lowered == QStringLiteral("starting") || listening || paused;
    // A review has buttons of its own.
    const bool cancelVisible = dictationCancelable(lowered) && lowered != QStringLiteral("reviewing");
    return {pauseVisible, listening || paused, paused, cancelVisible,
            cancelVisible && !pauseVisible};
}

QString pauseCaption()
{
    return QStringLiteral("Pause");
}

QString resumeCaption()
{
    return QStringLiteral("Resume");
}

QString cancelCaption()
{
    return QStringLiteral("Cancel");
}

QString trayToolTip(bool listening, bool recording)
{
    if (listening && recording) {
        return QStringLiteral("Speecher is listening and recording");
    }
    if (recording) {
        return QStringLiteral("Speecher is recording");
    }
    return listening ? QStringLiteral("Speecher is listening") : QStringLiteral("Speecher");
}

QString traySettingsCaption()
{
    return QStringLiteral("Settings…");
}

QString trayQuitCaption()
{
    return QStringLiteral("Quit Speecher");
}

QString copyTranscriptCaption()
{
    return QStringLiteral("Copy transcript");
}

QString copiedCaption()
{
    return QStringLiteral("Copied");
}

QString noTranscriptYetText()
{
    return QStringLiteral("Nothing dictated yet.");
}

QString inputLevelLabel()
{
    return QStringLiteral("Input level");
}

QString dictationStateLabel(DictationState state, const QString &message)
{
    if (state == DictationState::Error) {
        return message;
    }
    QString name = dictationStateName(state);
    name.replace(0, 1, name.left(1).toUpper());
    return name;
}

} // namespace speecher
