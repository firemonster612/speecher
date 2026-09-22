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

// What a tray Start/Stop control presents for a session state name, matching
// what toggle() would actually do (DictationSession::toggleSession): it stops
// starting, listening and refining — a toggle mid-refinement cancels the
// refinement — and does nothing during stopping and delivering.
struct DictationToggleAction {
    QString label;
    bool enabled = true;
};

DictationToggleAction dictationToggleAction(const QString &stateName);

struct SessionResponse {
    bool ok = true;
    QString state;
    QString message;
};

} // namespace speecher
