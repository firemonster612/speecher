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

// What a Start/Stop control presents for a session state name, matching
// what toggle() would actually do (DictationSession::toggleSession): it stops
// starting and listening, cancels a refinement, and does nothing during
// stopping and delivering.
struct DictationToggleAction {
    QString label;
    bool enabled = true;
};

DictationToggleAction dictationToggleAction(const QString &stateName);

// What a status line (Home, a tray panel's heading) says for a session state
// name. message is the session's own words, DictationSession::lastMessage():
// an error's reason, or while delivering the receipt ("Input sent"); the
// other states ignore it.
QString dictationStatusLabel(const QString &stateName, const QString &message = {});

// Whether a tray shows the listening icon, tooltip and level meter for a
// session state name: the states where the microphone is open or about to be.
bool dictationListeningPresentation(const QString &stateName);

// The captions every tray menu, tray panel and menu bar panel shares.
QString trayToolTip(bool listening);
QString traySettingsCaption();
QString trayQuitCaption();
QString copyTranscriptCaption();
// What a copy button says for a moment after it copied.
QString copiedCaption();
inline constexpr int kCopiedFeedbackMs = 1500;
// Where a tray panel's last transcript goes before there is one.
QString noTranscriptYetText();

struct SessionResponse {
    bool ok = true;
    QString state;
    QString message;
};

} // namespace speecher
