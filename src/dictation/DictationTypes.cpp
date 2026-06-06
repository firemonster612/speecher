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
