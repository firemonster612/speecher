#pragma once

#include "core/OutputFormat.h"
#include "core/ProviderChain.h"
#include "core/ProviderFailure.h"
#include "core/ShortcutBinding.h"

#include <QList>
#include <QString>
#include <QStringList>

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
    // A selection edit waits for Replace or Keep original. Last, so the
    // macOS bridge's mirror of these values keeps the ones before it.
    Reviewing,
};

QString dictationStateName(DictationState state);
QString dictationStateLabel(DictationState state, const QString &message = {});

// What a Start/Stop control presents for a session state name, matching
// what toggle() would actually do (DictationSession::toggleSession): it stops
// starting and listening, cancels a refinement, dictates a follow-up to an
// edit under review, and does nothing during stopping and delivering, when
// it is disabled and names the state instead.
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

// Whether a session state name is one a cancel would throw away, Starting
// through Refining, or Reviewing, where cancel keeps the original selection:
// trays offer Cancel then, and Windows and macOS take Escape.
bool dictationCancelable(const QString &stateName);
// Whether pause or resume applies: Listening or Paused.
bool dictationPausable(const QString &stateName);

// Which of Escape and Enter a session wants taken from the desktop.
struct SessionKeys {
    bool escape = false;
    bool enter = false;

    bool operator==(const SessionKeys &other) const = default;
};
// Escape cancels a cancelable session where the platform takes it for the
// whole session, unless the Cancel or Pause Shortcut is Escape: both cannot
// hold it at once. While a selection edit is up for review, a follow-up to it
// dictated or not, Escape is taken on every platform, since there it keeps
// the review rather than reaching the Target and its selection; only a
// Cancel Shortcut of Escape, which does the same, leaves it be, and a Pause
// Shortcut of Escape while the follow-up can be paused. Enter
// replaces while the edit waits. Nothing is taken while the session
// shortcuts are suspended, as while one is being recorded.
SessionKeys sessionKeysWanted(const QString &stateName,
                              bool reviewUp,
                              bool escapeCancelsDictation,
                              const ShortcutBinding &cancelShortcut,
                              const ShortcutBinding &pauseShortcut,
                              bool suspended);

// The tray icon's tooltip and Quit item. recording: a recording is running,
// which `speecher record` starts.
QString trayToolTip(bool listening, bool recording);
QString trayQuitCaption();

// Why a provider in a Dictation Session's chain did not do its part.
struct ProviderAttemptIssue {
    enum class Stage {
        // Its preparation failed.
        Prepare,
        // It can't listen for the Spoken Language, so it was passed over.
        LanguageSkipped,
        // It failed before it heard any of the dictation.
        Connect,
        // It stopped after it had started hearing the dictation.
        Interrupted,
    };
    ProviderRole role = ProviderRole::Speech;
    QString providerId;
    Stage stage = Stage::Prepare;
    ProviderFailureKind kind = ProviderFailureKind::Unavailable;
    // The provider's own reason.
    QString message;
    // It needs the internet and could not connect, or was passed over,
    // while the system reported none; only then may an outcome say "No
    // internet".
    bool offline = false;
    // A speech provider that never connected still took audio the
    // microphone heard, which is never sent again (rule A7): a few words may
    // be missing. Always so for one Interrupted.
    bool wordsLost = false;
};

// Which providers a Dictation Session used and which let it down.
struct ProviderHistory {
    // By id, in the order they ran, each once. A provider ran once its
    // startAttempt() or refine() was called.
    QStringList speechRan;
    QStringList refinementRan;
    QList<ProviderAttemptIssue> issues;
};

struct SessionResponse {
    bool ok = true;
    QString state;
    QString message;
};

} // namespace speecher
