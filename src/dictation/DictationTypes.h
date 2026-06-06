#pragma once

#include <QString>

namespace speecher {

enum class DictationState {
    Idle,
    Starting,
    Listening,
    Stopping,
    Refining,
    Delivering,
    Error,
};

QString dictationStateName(DictationState state);
QString dictationStateLabel(DictationState state, const QString &message = {});

struct SessionResponse {
    bool ok = true;
    QString state;
    QString message;
};

} // namespace speecher
