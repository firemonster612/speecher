#include "frontend/quick/host/QuickRecording.h"

#include "app/ApplicationController.h"
#include "app/PlatformComposition.h"
#include "core/SettingsStore.h"
#include "providers/ProviderRegistry.h"
#include "recording/RecordingPresentation.h"
#include "recording/RecordingSession.h"

namespace speecher {

QuickRecording::QuickRecording(ApplicationController *controller, QObject *parent)
    : QObject(parent)
    , m_controller(controller)
{
    RecordingSession *recording = controller->recording();
    m_clock.setInterval(1000);
    connect(&m_clock, &QTimer::timeout, this, &QuickRecording::elapsedChanged);
    connect(recording, &RecordingSession::phaseChanged, this, [this] {
        const QString now = state();
        if (now == u"starting") {
            m_lines.clear();
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
    });
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
        if (error != m_problem) {
            m_problem = error;
            emit problemChanged();
        }
    });
    connect(controller->settings(), &SettingsStore::snapshotApplied, this, &QuickRecording::planChanged);
}

QString QuickRecording::state() const
{
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
    if (m_controller->setupPending()) {
        return recordingNeedsSetupText();
    }
    return plan().value(QStringLiteral("service")).toString().isEmpty() ? recordingNoServiceText() : QString();
}

QVariantMap QuickRecording::plan() const
{
    const AppSettings settings = m_controller->settings()->dictationSnapshot();
    const ServiceOrder services =
        planRecording(settings, {}, streamsFinalText(*m_controller->providerRegistry())).speech.services;
    const RecordingSettings &recording = settings.recording;
    return {
        {QStringLiteral("service"), services.main()},
        {QStringLiteral("model"), services.choice(services.main()).model},
        {QStringLiteral("sources"),
         recording.systemAudio && canRecordCallAudio() ? QStringLiteral("both") : QStringLiteral("mic")},
        {QStringLiteral("folder"), recordingFolder(recording)},
        {QStringLiteral("format"),
         recording.format == RecordingFormat::PlainText ? QStringLiteral("txt") : QStringLiteral("md")},
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
    m_controller->recording()->stop();
}

void QuickRecording::confirmConsent()
{
    m_controller->settings()->setRecordingConsentNoticeShown(true);
    emit consentNeededChanged();
}

} // namespace speecher
