#pragma once

#include "core/OutputFormat.h"

#include <QString>

#include <optional>

namespace speecher {

// What `toggle` and `start` on the command line change for one Dictation
// Session. Nothing is saved.
struct SessionOverrides {
    std::optional<OutputFormat> outputFormat;
    // A Writing Profile id.
    std::optional<QString> writingProfile;
    // A Spoken Language: a language code, or kAutomaticSpokenLanguage.
    std::optional<QString> spokenLanguage;
};

enum class DictationState {
    Idle,
    Starting,
    Listening,
    // The microphone is off and the words so far are kept; resume listens on.
    Paused,
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
// stopping and delivering, when it is disabled and names the state instead.
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
// The last failure (DictationSession::lastFailure) as Home shows it under the
// status line: empty in the error state, whose status line already says it.
QString dictationFailureNote(const QString &stateName, const QString &lastFailure);

// The line under Home's status: how to start dictating from anywhere, or that
// a Global Shortcut would let you. shortcut is its display text, empty for none.
QString dictationShortcutHint(const QString &shortcut);

// Whether a tray shows the listening icon, tooltip and level meter for a
// session state name: the states where the microphone is open or about to be.
bool dictationListeningPresentation(const QString &stateName);

// Whether a session state name is one a cancel would throw away, Starting
// through Refining: trays offer Cancel then, and Windows and macOS take Escape.
bool dictationCancelable(const QString &stateName);
// The trays' Cancel item.
QString cancelDictationCaption();

// The buttons either side of the waveform, in the popup and on Home's
// listening row: pause on the left, which resumes while paused, and cancel on
// the right. Pause shows from Starting to Paused and works once listening;
// cancel shows while a cancel would throw the session away.
struct SessionControls {
    bool pauseVisible = false;
    bool pauseEnabled = false;
    // The pause button resumes instead.
    bool paused = false;
    bool cancelVisible = false;
};
SessionControls sessionControls(const QString &stateName);
// The buttons' tooltips and accessible names.
QString pauseCaption();
QString resumeCaption();
QString cancelCaption();

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
// The accessible name of a microphone level meter, in the tray panels and the
// setup assistant's microphone step.
QString inputLevelLabel();

struct SessionResponse {
    bool ok = true;
    QString state;
    QString message;
};

} // namespace speecher
