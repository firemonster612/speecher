#include "dictation/DictationTypes.h"

namespace speecher {

QString dictationStateName(DictationState state)
{
    switch (state) {
    case DictationState::Idle: return QStringLiteral("idle");
    case DictationState::Starting: return QStringLiteral("starting");
    case DictationState::Listening: return QStringLiteral("listening");
    case DictationState::Stopping: return QStringLiteral("stopping");
    case DictationState::Refining: return QStringLiteral("refining");
    case DictationState::Delivering: return QStringLiteral("delivering");
    case DictationState::Error: return QStringLiteral("error");
    }
    return QStringLiteral("error");
}

DictationToggleAction dictationToggleAction(const QString &stateName)
{
    const QString lowered = stateName.toLower();
    if (lowered == QStringLiteral("starting") || lowered == QStringLiteral("listening")
        || lowered == QStringLiteral("refining")) {
        return {QStringLiteral("Stop Dictation"), true};
    }
    if (lowered == QStringLiteral("stopping") || lowered == QStringLiteral("delivering")) {
        return {QStringLiteral("Start Dictation"), false};
    }
    // Keyed on the state name because that is what the callers receive over
    // their state-change signals, so the compiler cannot enforce coverage:
    // an unrecognised or future state name deliberately falls through to an
    // enabled Start, which is also what idle and error present.
    return {QStringLiteral("Start Dictation"), true};
}

bool dictationListeningPresentation(const QString &stateName)
{
    const QString lowered = stateName.toLower();
    return lowered == QStringLiteral("starting") || lowered == QStringLiteral("listening");
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
