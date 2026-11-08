#pragma once

#include "frontend/quick/host/HostSingleton.h"

#include <QObject>
#include <QQmlEngine>
#include <QTimer>
#include <QVariant>

#include <utility>

namespace speecher {

class ApplicationController;

// The UI's Recording singleton: the recording running, started from the
// Recordings page, the tray, the Recording Shortcut or `speecher record`
// alike (its phase, name, file, time, streams and their levels, its lines as
// the file takes them, and what went wrong), what Record would run with now
// and why it can't, and starting and stopping one. Data only;
// pages/recordings/RecordingWords.js words it.
class QuickRecording final : public QObject, public HostSingleton<QuickRecording> {
    Q_OBJECT
    QML_NAMED_ELEMENT(Recording)
    QML_SINGLETON
    // off, starting, recording or stopping.
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    // The running recording's name (empty without one) and file.
    Q_PROPERTY(QString title READ title NOTIFY stateChanged)
    Q_PROPERTY(QString path READ path NOTIFY stateChanged)
    // How long it has run, ticking each second.
    Q_PROPERTY(qint64 elapsedMs READ elapsedMs NOTIFY elapsedChanged)
    // [{ speaker (me, them), state (recording, paused, reconnecting,
    // stopped), problem, lostAudioMs }], the microphone first.
    Q_PROPERTY(QVariantList streams READ streams NOTIFY problemsChanged)
    // { me, them }: each stream's input level, 0 to 1.
    Q_PROPERTY(QVariantMap levels READ levels NOTIFY levelsChanged)
    // The recording's lines so far, [{ atMs, speaker, text }], as it
    // started; lineAdded brings each one after.
    Q_PROPERTY(QVariantList lines READ lines NOTIFY stateChanged)
    // Core's words for echo that stays in the microphone, and for the lines
    // the file could not take.
    Q_PROPERTY(QString echoWarning READ echoWarning NOTIFY problemsChanged)
    Q_PROPERTY(int unwrittenLines READ unwrittenLines NOTIFY problemsChanged)
    Q_PROPERTY(QString writeError READ writeError NOTIFY problemsChanged)
    // Why the last start failed, in core's words, until the next start. A
    // start cancelled while it connected leaves none.
    Q_PROPERTY(QString problem READ problem NOTIFY problemChanged)
    // Why Record can't start now, in core's words (setup, no service that
    // can record), or empty.
    Q_PROPERTY(QString refusal READ refusal NOTIFY planChanged)
    // Setup hasn't finished, which Record waits for whatever else is ready.
    // What Record would run with: { service, model, sources (both, mic),
    // profile (the Writing Profile whose vocabulary it takes),
    // inheritedProfile (Other apps', which it takes unless one is chosen), folder,
    // folderUrl, format (md, txt), autoStopMinutes }.
    Q_PROPERTY(QVariantMap plan READ plan NOTIFY planChanged)
    // The plan the recording running started with, which later changes
    // leave alone; empty while none runs.
    Q_PROPERTY(QVariantMap running READ running NOTIFY stateChanged)
    // Whether this computer captures what it plays, as "them".
    Q_PROPERTY(bool canRecordCallAudio READ canRecordCallAudio CONSTANT)
    // The consent notice waits to be shown before the first recording ever.
    Q_PROPERTY(bool consentNeeded READ consentNeeded NOTIFY consentNeededChanged)
    // The History Entry of the recording running or last run; 0 for none.
    Q_PROPERTY(qint64 lastEntry READ lastEntry NOTIFY stateChanged)

public:
    explicit QuickRecording(ApplicationController *controller, QObject *parent = nullptr);

    QString state() const;
    QString title() const;
    QString path() const;
    qint64 elapsedMs() const;
    QVariantList streams() const;
    QVariantMap levels() const { return m_levels; }
    QVariantList lines() const { return m_lines; }
    QString echoWarning() const;
    int unwrittenLines() const;
    QString writeError() const;
    QString problem() const { return m_problem; }
    QString refusal() const;
    QVariantMap plan() const;
    QVariantMap running() const { return m_running; }
    bool canRecordCallAudio() const;
    bool consentNeeded() const;
    qint64 lastEntry() const;

    // Starts a recording named title (trimmed; empty for none) with the saved
    // settings; problem says why one didn't start.
    Q_INVOKABLE void start(const QString &title);
    // Stops the recording running, or cancels one still connecting.
    Q_INVOKABLE void stop();
    // The folder transcripts go in, as the folder chooser gives it.
    Q_INVOKABLE void setFolder(const QUrl &folder);
    // The consent notice was shown and accepted.
    Q_INVOKABLE void confirmConsent();

signals:
    void stateChanged();
    void elapsedChanged();
    void problemsChanged();
    void levelsChanged();
    void problemChanged();
    void planChanged();
    void consentNeededChanged();
    // { atMs, speaker, text }: a line the file just took.
    void lineAdded(const QVariantMap &line);
    // Main on pageId, landing on rowId when it names one; the popup's pill
    // asks for it, as it has no window of its own to open pages in.
    void pageRequested(const QString &pageId, const QString &rowId);

private:
    // Each stream's lostAudioMs and unwrittenLines, which the clock watches.
    std::pair<QList<qint64>, int> problemCounts() const;

    ApplicationController *m_controller;
    QVariantList m_lines;
    QVariantMap m_levels;
    QString m_problem;
    QVariantMap m_running;
    QTimer m_clock;
    std::pair<QList<qint64>, int> m_problemCounts;
};

} // namespace speecher
