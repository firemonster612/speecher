#pragma once

#include "providers/LocalSpeechEngine.h"

#include <QList>
#include <QObject>
#include <QThread>
#include <QTimer>

#include <atomic>
#include <functional>
#include <memory>

namespace speecher {

// The thread every LocalSpeechTranscriber runs inference on, and the one
// Local Model loaded there, so a model is in memory once however many
// dictations, recordings and Speed Tests use it. Each user has a session of
// its own on the model, and the thread runs one call at a time, as
// transcribe.cpp asks of sessions sharing a model. The model unloads once no
// attempt has run for the idle time.
class LocalSpeechWorker final : public QObject {
    Q_OBJECT

public:
    explicit LocalSpeechWorker(std::unique_ptr<LocalSpeechEngine> engine = LocalSpeechEngine::create(),
                               QObject *parent = nullptr);
    // Waits for the call running to stop. The objects moved here that are
    // waiting to be deleted go with the thread, before the model.
    ~LocalSpeechWorker() override;

    // Moves object onto the worker thread.
    void adopt(QObject *object);

    // GUI thread: an attempt of any transcriber starts or ends. Unloading
    // waits while one runs, and idleUnloadMinutes after the last, the latest
    // attempt's setting; 0 keeps the model loaded.
    void attemptStarted(int idleUnloadMinutes);
    void attemptEnded();
    // Any thread.
    bool attemptRunning() const;
    // GUI thread: the Speed Test keeps the model loaded while it runs.
    void pauseIdleUnload();
    void resumeIdleUnload();

    // Worker thread only: one user's session on the model.
    class Client {
    public:
        // shouldAbort is polled during the session's calls.
        Client(LocalSpeechWorker &worker, std::function<bool()> shouldAbort);
        ~Client();
        Client(const Client &) = delete;
        Client &operator=(const Client &) = delete;

        // Loads modelPath unless it is loaded, and holds it until release():
        // meanwhile no other client loads another model in its place. Fails
        // while another client holds a different one.
        bool hold(const QString &modelPath, const LocalRunsOn &runsOn, QString *error);
        void release();
        // While held.
        bool streams() const;
        LocalSpeechEngine::Session &session();

    private:
        friend class LocalSpeechWorker;

        LocalSpeechWorker &m_worker;
        std::function<bool()> m_shouldAbort;
        // Opened by hold(), and closed whenever the model unloads.
        std::unique_ptr<LocalSpeechEngine::Session> m_session;
        bool m_held = false;
        bool m_registered = false;
    };

signals:
    // After every load and unload: where the model now runs, empty for
    // nowhere. Emitted on the worker thread.
    void runsOnChanged(const QString &description);

private:
    // Worker thread only.
    // Whether a client holds the model, besides the one given.
    bool someoneHolds(const Client *besides = nullptr) const;
    void closeSessions();
    void unloadIfUnused();
    void announceRunsOn();

    void startIdleTimer();

    std::atomic<int> m_attempts = 0;
    QTimer m_idleTimer;

    QThread m_thread;
    // Lives on m_thread.
    QObject m_context;
    // Everything below belongs to the worker thread.
    std::unique_ptr<LocalSpeechEngine> m_engine;
    QList<Client *> m_clients;
};

} // namespace speecher
