#include "frontend/quick/host/QuickRecording.h"

#include "app/ApplicationController.h"
#include "app/PlatformComposition.h"
#include "core/Apps.h"
#include "core/SettingsStore.h"
#include "providers/ProviderRegistry.h"
#include "recording/RecordingPresentation.h"
#include "recording/RecordingSession.h"

#include <QUrl>

namespace speecher {

QuickRecording::QuickRecording(ApplicationController *controller, QObject *parent)
    : QObject(parent)
    , m_controller(controller)
{
    RecordingSession *recording = controller->recording();
    m_clock.setInterval(1000);
    connect(&m_clock, &QTimer::timeout, this, [this] {
        emit elapsedChanged();
        // The session signals only a stream's first lost audio and the first
        // unwritten line; the counts after that grow quietly.
        const auto counts = problemCounts();
        if (counts != m_problemCounts) {
            m_problemCounts = counts;
            emit problemsChanged();
        }
    });
    const auto phaseChanged = [this] {
        const QString now = state();
        if (now == u"starting") {
            m_lines.clear();
            m_running = plan();
        } else if (now == u"off") {
            m_running.clear();
        }
        if (now == u"off") {
            m_levels.clear();
            emit levelsChanged();
        }
        (now == u"recording" || now == u"stopping") ? m_clock.start() : m_clock.stop();
        emit stateChanged();
        emit elapsedChanged();
        emit problemsChanged();
        // The first `record start` ever shows the notice itself.
        emit consentNeededChanged();
    };
    connect(recording, &RecordingSession::phaseChanged, this, phaseChanged);
    connect(controller, &ApplicationController::recordingStartPendingChanged, this, phaseChanged);
    connect(recording, &RecordingSession::problemChanged, this, &QuickRecording::problemsChanged);
    connect(recording, &RecordingSession::lineWritten, this,
            [this](qint64 atMs, const QString &speaker, const QString &text) {
                const QVariantMap line{{QStringLiteral("atMs"), atMs},
                                       {QStringLiteral("speaker"), speaker},
                                       {QStringLiteral("text"), text}};
                m_lines.append(line);
                emit lineAdded(line);
            });
    connect(recording, &RecordingSession::levelChanged, this, [this](const QString &speaker, float level) {
        m_levels.insert(speaker, level);
        emit levelsChanged();
    });
    connect(controller, &ApplicationController::recordingStartFinished, this, [this](const QString &error) {
        const QString problem = error == recordingCancelledText() ? QString() : error;
        if (problem != m_problem) {
            m_problem = problem;
            emit problemChanged();
        }
    });
    connect(controller->settings(), &SettingsStore::snapshotApplied, this, &QuickRecording::planChanged);
}

QString QuickRecording::state() const
{
    if (m_controller->recordingStartPending()) {
        return QStringLiteral("starting");
    }
    switch (m_controller->recording()->phase()) {
    case RecordingSession::Phase::Starting:
        return QStringLiteral("starting");
    case RecordingSession::Phase::Recording:
        return QStringLiteral("recording");
    case RecordingSession::Phase::Stopping:
        return QStringLiteral("stopping");
    case RecordingSession::Phase::Off:
        break;
    }
    return QStringLiteral("off");
}

QString QuickRecording::title() const
{
    return m_controller->recording()->status().title;
}

QString QuickRecording::path() const
{
    return m_controller->recording()->status().path;
}

qint64 QuickRecording::elapsedMs() const
{
    return m_controller->recording()->status().durationMs;
}

QVariantList QuickRecording::streams() const
{
    QVariantList streams;
    for (const RecordingStreamStatus &stream : m_controller->recording()->status().streams) {
        streams.append(QVariantMap{{QStringLiteral("speaker"), stream.speaker},
                                   {QStringLiteral("state"), recordingStreamStateName(stream.state)},
                                   {QStringLiteral("problem"), stream.problem},
                                   {QStringLiteral("lostAudioMs"), stream.lostAudioMs}});
    }
    return streams;
}

QString QuickRecording::echoWarning() const
{
    return m_controller->recording()->status().echoCancellationWarning;
}

std::pair<QList<qint64>, int> QuickRecording::problemCounts() const
{
    const RecordingStatus status = m_controller->recording()->status();
    QList<qint64> lostAudio;
    for (const RecordingStreamStatus &stream : status.streams) {
        lostAudio.append(stream.lostAudioMs);
    }
    return {lostAudio, status.unwrittenLines};
}

int QuickRecording::unwrittenLines() const
{
    return m_controller->recording()->status().unwrittenLines;
}

QString QuickRecording::writeError() const
{
    return m_controller->recording()->status().writeError;
}

QString QuickRecording::refusal() const
{
    return plan().value(QStringLiteral("service")).toString().isEmpty() ? recordingNoServiceText() : QString();
}

QVariantMap QuickRecording::plan() const
{
    const AppSettings settings = m_controller->settings()->dictationSnapshot();
    const ServiceOrder services =
        planRecording(settings, {}, streamsFinalText(*m_controller->providerRegistry())).speech.services;
    const RecordingSettings &recording = settings.recording;
    const QString folder = recordingFolder(recording);
    return {
        {QStringLiteral("service"), services.main()},
        {QStringLiteral("model"), services.choice(services.main()).model},
        {QStringLiteral("sources"),
         recording.systemAudio && canRecordCallAudio() ? QStringLiteral("both") : QStringLiteral("mic")},
        {QStringLiteral("profile"),
         recording.writingProfile.isEmpty() ? fallbackWritingProfile(settings.apps) : recording.writingProfile},
        {QStringLiteral("inheritedProfile"), fallbackWritingProfile(settings.apps)},
        {QStringLiteral("folder"), folder},
        {QStringLiteral("folderUrl"), QUrl::fromLocalFile(folder)},
        {QStringLiteral("format"),
         recording.format == RecordingFormat::PlainText ? QStringLiteral("txt") : QStringLiteral("md")},
        {QStringLiteral("autoStopMinutes"), recording.autoStopMinutes},
    };
}

bool QuickRecording::canRecordCallAudio() const
{
    return m_controller->platform()->recordsSystemAudio();
}

bool QuickRecording::consentNeeded() const
{
    return !m_controller->settings()->recordingConsentNoticeShown();
}

qint64 QuickRecording::lastEntry() const
{
    return m_controller->recordingHistoryId();
}

void QuickRecording::start(const QString &title)
{
    m_controller->startRecording({{}, title.trimmed()}, [](const QString &) {});
}

void QuickRecording::stop()
{
    m_controller->stopRecording();
}

void QuickRecording::setFolder(const QUrl &folder)
{
    if (!folder.isLocalFile()) {
        return;
    }
    m_controller->settings()->edit([&folder](AppSettings &draft) {
        draft.recording.folder = folder.toLocalFile();
        return true;
    });
}

void QuickRecording::confirmConsent()
{
    m_controller->settings()->setRecordingConsentNoticeShown(true);
    emit consentNeededChanged();
}

} // namespace speecher
