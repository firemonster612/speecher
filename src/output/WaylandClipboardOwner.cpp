#include "output/WaylandClipboardOwner.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QProcess>
#include <QTimer>

#include <utility>

namespace speecher::WaylandClipboardOwner {

namespace {

constexpr int ownerReadyTimeoutMs = 4000;
// A replaced helper normally exits on its own once the compositor tells it
// the selection moved on. Past this it is asked to stop, and killed after
// twice as long.
constexpr int retireGraceMs = 500;

QPointer<QProcess> activeClipboardOwner;

// Lets process go without waiting for it: it is deleted once it has exited,
// asked to stop if it still runs after the grace period, and then killed.
void retire(QProcess *process)
{
    if (!process) {
        return;
    }
    if (process->state() == QProcess::NotRunning) {
        process->deleteLater();
        return;
    }
    QObject::connect(process, &QProcess::finished, process, &QObject::deleteLater);
    QTimer::singleShot(retireGraceMs, process, &QProcess::terminate);
    QTimer::singleShot(2 * retireGraceMs, process, &QProcess::kill);
}

// The quit path, the one place that waits: nothing is left to keep responsive.
void stopActiveClipboardOwner()
{
    QProcess *process = std::exchange(activeClipboardOwner, nullptr);
    if (!process) {
        return;
    }
    if (process->state() != QProcess::NotRunning && !process->waitForFinished(500)) {
        process->terminate();
        if (!process->waitForFinished(500)) {
            process->kill();
            process->waitForFinished(500);
        }
    }
    delete process;
}

QByteArray ownerPayload(const QList<ClipboardMimePart> &parts)
{
    QJsonArray encodedParts;
    for (const ClipboardMimePart &part : parts) {
        encodedParts.append(QJsonObject{
            {QStringLiteral("mime"), part.mimeType},
            {QStringLiteral("data"), QString::fromLatin1(part.data.toBase64())},
        });
    }
    return QJsonDocument(QJsonObject{
        {QStringLiteral("parts"), encodedParts},
    }).toJson(QJsonDocument::Compact);
}

// One helper from its start until it says READY or fails. Its connections
// end with it, so nothing it hears afterwards counts.
class OwnerStart final : public QObject {
public:
    OwnerStart(const QString &helper,
               const QList<ClipboardMimePart> &parts,
               QObject *context,
               std::function<void(bool, const QString &)> done)
        // The application's, so none outlives it, even one let go just
        // before it quits.
        : m_process(new QProcess(QCoreApplication::instance()))
        , m_publishesSelection(!parts.isEmpty())
        , m_context(context)
        , m_done(std::move(done))
    {
        m_process->setProcessChannelMode(QProcess::SeparateChannels);
        connect(m_process, &QProcess::started, this, [this, payload = ownerPayload(parts)] {
            m_process->write(payload);
            m_process->closeWriteChannel();
        });
        connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) {
                fail(QStringLiteral("Could not start Speecher's Wayland clipboard helper"));
            }
        });
        connect(m_process, &QProcess::readyReadStandardOutput, this, [this] {
            if (!m_process->canReadLine()) {
                return;
            }
            if (m_process->readLine().trimmed() == QByteArrayLiteral("READY")) {
                publish();
            } else {
                failWithHelperError();
            }
        });
        connect(m_process, &QProcess::finished, this, &OwnerStart::failWithHelperError);
        QTimer::singleShot(ownerReadyTimeoutMs, this, &OwnerStart::failWithHelperError);
        m_process->start(helper);
    }

private:
    void publish()
    {
        if (!m_done) {
            return;
        }
        retire(std::exchange(activeClipboardOwner, m_publishesSelection ? m_process : nullptr));
        // A helper that cleared the selection has nothing left to serve.
        if (!m_publishesSelection) {
            retire(m_process);
        }
        answer(true, {});
    }

    void failWithHelperError()
    {
        const QString stderrText = QString::fromUtf8(m_process->readAllStandardError()).trimmed();
        fail(stderrText.isEmpty() ? QStringLiteral("Wayland clipboard helper did not publish the selection")
                                  : stderrText);
    }

    void fail(const QString &error)
    {
        if (!m_done) {
            return;
        }
        m_process->kill();
        retire(m_process);
        answer(false, error);
    }

    // Once: a timer can still fire before this is deleted.
    void answer(bool published, const QString &error)
    {
        const auto done = std::exchange(m_done, {});
        deleteLater();
        disconnect(m_process, nullptr, this, nullptr);
        if (m_context) {
            done(published, error);
        }
    }

    QProcess *m_process;
    bool m_publishesSelection;
    QPointer<QObject> m_context;
    std::function<void(bool, const QString &)> m_done;
};

} // namespace

void start(const QString &helper,
           const QList<ClipboardMimePart> &parts,
           QObject *context,
           std::function<void(bool published, const QString &error)> done)
{
    static const bool cleanupRegistered = [] {
        qAddPostRoutine(stopActiveClipboardOwner);
        return true;
    }();
    Q_UNUSED(cleanupRegistered);

    new OwnerStart(helper, parts, context, std::move(done));
}

} // namespace speecher::WaylandClipboardOwner
