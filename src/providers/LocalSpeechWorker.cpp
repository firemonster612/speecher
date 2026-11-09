#include "providers/LocalSpeechWorker.h"

#include <algorithm>

namespace speecher {
namespace {

constexpr int msPerMinute = 60 * 1000;
// Keeps the timer's millisecond interval well inside an int.
constexpr int maxIdleUnloadMinutes = 24 * 60;

int idleUnloadMs(int minutes)
{
    return std::clamp(minutes, 0, maxIdleUnloadMinutes) * msPerMinute;
}

} // namespace

LocalSpeechWorker::LocalSpeechWorker(std::unique_ptr<LocalSpeechEngine> engine, QObject *parent)
    : QObject(parent)
    , m_engine(std::move(engine))
{
    m_idleTimer.setSingleShot(true);
    // Until an attempt brings the person's setting.
    m_idleTimer.setInterval(idleUnloadMs(LocalSpeechSettings().idleUnloadMinutes));
    connect(&m_idleTimer, &QTimer::timeout, this, [this] {
        QMetaObject::invokeMethod(&m_context, [this] { unloadIfUnused(); }, Qt::QueuedConnection);
    });
    m_thread.setObjectName(QStringLiteral("LocalSpeech"));
    m_context.moveToThread(&m_thread);
    m_thread.start();
}

LocalSpeechWorker::~LocalSpeechWorker()
{
    m_thread.quit();
    m_thread.wait();
    // Sessions go before the model.
    Q_ASSERT(m_clients.isEmpty());
}

void LocalSpeechWorker::adopt(QObject *object)
{
    object->moveToThread(&m_thread);
}

void LocalSpeechWorker::attemptStarted(int idleUnloadMinutes)
{
    ++m_attempts;
    m_idleTimer.stop();
    m_idleTimer.setInterval(idleUnloadMs(idleUnloadMinutes));
}

void LocalSpeechWorker::attemptEnded()
{
    if (--m_attempts == 0) {
        startIdleTimer();
    }
}

bool LocalSpeechWorker::attemptRunning() const
{
    return m_attempts.load() > 0;
}

void LocalSpeechWorker::pauseIdleUnload()
{
    m_idleTimer.stop();
}

void LocalSpeechWorker::resumeIdleUnload()
{
    if (!attemptRunning()) {
        startIdleTimer();
    }
}

void LocalSpeechWorker::startIdleTimer()
{
    if (m_idleTimer.interval() > 0) {
        m_idleTimer.start();
    }
}

bool LocalSpeechWorker::someoneHolds(const Client *besides) const
{
    return std::any_of(m_clients.cbegin(), m_clients.cend(),
                       [besides](const Client *client) { return client != besides && client->m_held; });
}

// Sessions go before the model they were opened on.
void LocalSpeechWorker::closeSessions()
{
    for (Client *client : std::as_const(m_clients)) {
        client->m_session.reset();
    }
}

void LocalSpeechWorker::unloadIfUnused()
{
    if (attemptRunning() || someoneHolds()) {
        return;
    }
    closeSessions();
    m_engine->unload();
    announceRunsOn();
}

void LocalSpeechWorker::announceRunsOn()
{
    emit runsOnChanged(m_engine->runsOnDescription());
}

LocalSpeechWorker::Client::Client(LocalSpeechWorker &worker, std::function<bool()> shouldAbort)
    : m_worker(worker)
    , m_shouldAbort(std::move(shouldAbort))
{
}

LocalSpeechWorker::Client::~Client()
{
    m_session.reset();
    m_worker.m_clients.removeOne(this);
}

bool LocalSpeechWorker::Client::hold(const QString &modelPath, const LocalRunsOn &runsOn, QString *error)
{
    if (!std::exchange(m_registered, true)) {
        m_worker.m_clients.append(this);
    }
    LocalSpeechEngine &engine = *m_worker.m_engine;
    if (!engine.isLoaded(modelPath, runsOn)) {
        if (m_worker.someoneHolds(this)) {
            *error = QStringLiteral("This computer is already transcribing with another model, and runs one "
                                    "model at a time.");
            return false;
        }
        m_worker.closeSessions();
        const bool loaded = engine.load(modelPath, runsOn, error);
        m_worker.announceRunsOn();
        if (!loaded) {
            return false;
        }
    }
    if (!m_session) {
        m_session = engine.openSession(m_shouldAbort, error);
        if (!m_session) {
            return false;
        }
    }
    m_held = true;
    return true;
}

void LocalSpeechWorker::Client::release()
{
    m_held = false;
}

bool LocalSpeechWorker::Client::streams() const
{
    return m_worker.m_engine->streams();
}

LocalSpeechEngine::Session &LocalSpeechWorker::Client::session()
{
    return *m_session;
}

} // namespace speecher
