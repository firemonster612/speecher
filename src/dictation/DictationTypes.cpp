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

DictationToggleAction dictationToggleAction(DictationState state)
{
    switch (state) {
    case DictationState::Starting:
    case DictationState::Listening:
    case DictationState::Paused:
        return {QStringLiteral("Stop dictation"), true};
    case DictationState::Refining:
        return {QStringLiteral("Cancel refinement"), true};
    // As the Global Shortcut does then: a follow-up instruction.
    case DictationState::Reviewing:
        return {QStringLiteral("Ask for more changes"), true};
    // Nothing to do while the text is on its way, so the control says why.
    case DictationState::Stopping:
    case DictationState::Delivering:
        return {dictationStatusLabel(state), false};
    case DictationState::Idle:
    case DictationState::Error:
        break;
    }
    return {QStringLiteral("Start dictation"), true};
}

QString dictationStatusLabel(DictationState state, const QString &message)
{
    switch (state) {
    case DictationState::Starting:
    case DictationState::Listening:
        return QStringLiteral("Listening…");
    case DictationState::Paused:
        return QStringLiteral("Paused");
    case DictationState::Stopping:
        return QStringLiteral("Transcribing…");
    case DictationState::Refining:
        return QStringLiteral("Refining…");
    case DictationState::Delivering:
        return message.isEmpty() ? QStringLiteral("Delivering…") : message;
    case DictationState::Reviewing:
        return QStringLiteral("Review the edit in the popup");
    case DictationState::Error:
        return message.isEmpty() ? QStringLiteral("Dictation failed") : message;
    case DictationState::Idle:
        break;
    }
    return QStringLiteral("Idle");
}

bool dictationListeningPresentation(DictationState state)
{
    return state == DictationState::Starting || state == DictationState::Listening;
}

bool dictationCancelable(DictationState state)
{
    switch (state) {
    case DictationState::Starting:
    case DictationState::Listening:
    case DictationState::Paused:
    case DictationState::Stopping:
    case DictationState::Refining:
    case DictationState::Reviewing:
        return true;
    case DictationState::Idle:
    case DictationState::Delivering:
    case DictationState::Error:
        break;
    }
    return false;
}

bool dictationPausable(DictationState state)
{
    return state == DictationState::Listening || state == DictationState::Paused;
}

SessionKeys sessionKeysWanted(DictationState state,
                              bool reviewUp,
                              bool escapeCancelsDictation,
                              const ShortcutBinding &cancelShortcut,
                              const ShortcutBinding &pauseShortcut,
                              bool suspended)
{
    if (suspended) {
        return {};
    }
    const ShortcutBinding escape{QKeySequence(Qt::Key_Escape)};
    const bool waiting = state == DictationState::Reviewing;
    const bool cancels = dictationCancelable(state) && escapeCancelsDictation && pauseShortcut != escape;
    // A Pause Shortcut of Escape holds it while a follow-up can be paused.
    const bool keepsReview = reviewUp && !(dictationPausable(state) && pauseShortcut == escape);
    return {cancelShortcut != escape && (keepsReview || cancels), waiting};
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

QString trayQuitCaption()
{
    return QStringLiteral("Quit Speecher");
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
