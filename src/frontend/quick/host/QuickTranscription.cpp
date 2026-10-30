#include "frontend/quick/host/QuickTranscription.h"

#include "app/ApplicationController.h"
#include "core/SettingsStore.h"

#include <QClipboard>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMimeDatabase>
#include <QRegularExpression>

#include <utility>

namespace speecher {

namespace {

// Often enough for the bar to move smoothly through a wait.
constexpr int kProgressTickMs = 100;

const QList<std::pair<TranscriptDestination, QString>> kDestinations{
    {TranscriptDestination::BesideInput, QStringLiteral("beside")},
    {TranscriptDestination::Folder, QStringLiteral("folder")},
    {TranscriptDestination::None, QStringLiteral("none")},
};

const QList<std::pair<TranscriptFormat, QString>> kFormats{
    {TranscriptFormat::Text, QStringLiteral("text")},
    {TranscriptFormat::Srt, QStringLiteral("srt")},
    {TranscriptFormat::WebVtt, QStringLiteral("vtt")},
};

template <typename Enum>
QString idOf(const QList<std::pair<Enum, QString>> &ids, Enum value)
{
    for (const auto &[each, id] : ids) {
        if (each == value) {
            return id;
        }
    }
    return {};
}

template <typename Enum>
std::optional<Enum> valueOf(const QList<std::pair<Enum, QString>> &ids, const QString &id)
{
    for (const auto &[each, eachId] : ids) {
        if (eachId == id) {
            return each;
        }
    }
    return std::nullopt;
}

QString phaseId(TranscribePhase phase)
{
    switch (phase) {
    case TranscribePhase::Reading:
        return QStringLiteral("reading");
    case TranscribePhase::Transcribing:
        return QStringLiteral("transcribing");
    case TranscribePhase::Finishing:
        return QStringLiteral("finishing");
    case TranscribePhase::Refining:
        return QStringLiteral("refining");
    }
    return {};
}

QString fileStateId(TranscribeQueue::FileState state)
{
    switch (state) {
    case TranscribeQueue::FileState::Waiting:
        return QStringLiteral("waiting");
    case TranscribeQueue::FileState::Current:
        return QStringLiteral("current");
    case TranscribeQueue::FileState::Done:
        return QStringLiteral("done");
    case TranscribeQueue::FileState::Failed:
        return QStringLiteral("failed");
    }
    return {};
}

int wordCount(const QString &text)
{
    static const QRegularExpression space(QStringLiteral("\\s+"));
    return int(text.split(space, Qt::SkipEmptyParts).size());
}

} // namespace

QuickTranscription::QuickTranscription(ApplicationController *controller, QObject *parent)
    : QObject(parent)
    , m_controller(controller)
    , m_queue(*controller->transcribeQueue())
{
    m_progressClock.setInterval(kProgressTickMs);
    const auto followProgress = [this] {
        const qreal progress = m_queue.progress();
        if (!qFuzzyCompare(progress, m_progress)) {
            m_progress = progress;
            emit progressChanged();
        }
    };
    connect(&m_progressClock, &QTimer::timeout, this, followProgress);
    connect(&m_queue, &TranscribeQueue::fileChanged, this, [this, followProgress] {
        followProgress();
        emit fileChanged();
    });
    connect(&m_queue, &TranscribeQueue::stepChanged, this, [this] {
        if (m_queue.step() == TranscribeQueue::Step::Transcribe) {
            m_progressClock.start();
        } else {
            m_progressClock.stop();
        }
        // A new batch's results open on the refined transcripts.
        m_showRaw = false;
        emit batchChanged();
        emit fileChanged();
        emit resultsChanged();
        emit stepChanged();
    });
    connect(&m_queue, &TranscribeQueue::filesChanged, this, &QuickTranscription::filesChanged);
    connect(&m_queue, &TranscribeQueue::optionsChanged, this, &QuickTranscription::optionsChanged);
    connect(&m_queue, &TranscribeQueue::resultsChanged, this, [this] {
        emit resultsChanged();
        emit fileChanged();
    });
    connect(&m_queue, &TranscribeQueue::problemChanged, this, &QuickTranscription::problemChanged);
    // The refiner's model and the seeded language follow the settings.
    connect(controller->settings(), &SettingsStore::snapshotApplied, this, &QuickTranscription::optionsChanged);
}

void QuickTranscription::addFiles(const QStringList &paths)
{
    m_queue.addFiles(paths);
}

int QuickTranscription::step() const
{
    return int(m_queue.step());
}

namespace {

QVariantMap fileMap(const QString &path, qint64 durationMs)
{
    static const QMimeDatabase mimes;
    const QFileInfo info(path);
    return {
        {QStringLiteral("path"), path},
        {QStringLiteral("name"), info.fileName()},
        {QStringLiteral("kind"), mimes.mimeTypeForFile(info).name().startsWith(QStringLiteral("video/"))
                                     ? QStringLiteral("video")
                                     : QStringLiteral("audio")},
        {QStringLiteral("bytes"), info.size()},
        {QStringLiteral("durationMs"), durationMs},
    };
}

} // namespace

QVariantList QuickTranscription::files() const
{
    QVariantList files;
    for (const QString &path : m_queue.files()) {
        files << fileMap(path, m_queue.durationMs(path));
    }
    return files;
}

QStringList QuickTranscription::extensions() const
{
    return transcribableExtensions();
}

// The page binds to this on the GUI thread, so it never waits for the keyring.
QVariantMap QuickTranscription::options() const
{
    const TranscribeOptions &options = m_queue.options();
    return {
        {QStringLiteral("service"), options.speechProviderId},
        {QStringLiteral("vocabulary"), options.applyVocabulary},
        {QStringLiteral("refiner"), options.refinementProviderId},
        {QStringLiteral("cleanup"), options.cleanupStrength},
        {QStringLiteral("profile"), options.writingProfile},
        {QStringLiteral("tone"), options.tone},
        {QStringLiteral("language"),
         options.spokenLanguage.value_or(m_controller->settings()->dictationSnapshot().speech.language)},
        {QStringLiteral("destination"), idOf(kDestinations, options.destination)},
        {QStringLiteral("folder"), QDir::toNativeSeparators(options.folder)},
        {QStringLiteral("folderUrl"), options.folder.isEmpty() ? QUrl() : QUrl::fromLocalFile(options.folder)},
    };
}

QString QuickTranscription::speechModel() const
{
    return m_queue.speechModel();
}

QString QuickTranscription::refinerModel() const
{
    return m_queue.refinementModel();
}

QVariantMap QuickTranscription::batch() const
{
    QVariantList files;
    for (const QString &path : m_queue.batch()) {
        files << fileMap(path, m_queue.durationMs(path));
    }
    const TranscribeOptions &options = m_queue.batchOptions();
    return {
        {QStringLiteral("files"), files},
        {QStringLiteral("speech"), m_queue.batchLabels().speech},
        {QStringLiteral("refinement"), m_queue.batchLabels().refinement},
        {QStringLiteral("refines"), m_queue.batchRefines()},
        {QStringLiteral("destination"), idOf(kDestinations, options.destination)},
        {QStringLiteral("folder"), QDir::toNativeSeparators(options.folder)},
        {QStringLiteral("cancelled"), m_queue.cancelled()},
    };
}

int QuickTranscription::current() const
{
    return m_queue.current();
}

QString QuickTranscription::phase() const
{
    return phaseId(m_queue.phase());
}

QVariantList QuickTranscription::peaks() const
{
    QVariantList peaks;
    peaks.reserve(m_queue.peaks().size());
    for (float peak : m_queue.peaks()) {
        peaks << peak;
    }
    return peaks;
}

QString QuickTranscription::partial() const
{
    return m_queue.partialText();
}

QStringList QuickTranscription::queue() const
{
    QStringList states;
    for (int index = 0; index < m_queue.batch().size(); ++index) {
        states << fileStateId(m_queue.fileState(index));
    }
    return states;
}

qreal QuickTranscription::progress() const
{
    return m_progress;
}

QVariantList QuickTranscription::results() const
{
    QVariantList results;
    for (const TranscribeFileResult &result : m_queue.results()) {
        QVariantMap map = fileMap(result.path, m_queue.durationMs(result.path));
        const QString text = shownTranscript(result, m_showRaw);
        map.insert({
            {QStringLiteral("failed"), result.failed()},
            {QStringLiteral("error"), result.error},
            {QStringLiteral("text"), text},
            {QStringLiteral("words"), wordCount(text)},
            {QStringLiteral("timings"), canExportAs(result, TranscriptFormat::Srt)},
            {QStringLiteral("savedPath"), QDir::toNativeSeparators(result.savedPath)},
        });
        results << map;
    }
    return results;
}

int QuickTranscription::retrying() const
{
    return m_queue.retrying();
}

bool QuickTranscription::showRaw() const
{
    return m_showRaw;
}

void QuickTranscription::setShowRaw(bool raw)
{
    if (raw != m_showRaw) {
        m_showRaw = raw;
        emit resultsChanged();
    }
}

QString QuickTranscription::problem() const
{
    return m_queue.problem();
}

void QuickTranscription::addUrls(const QList<QUrl> &urls)
{
    QStringList paths;
    for (const QUrl &url : urls) {
        if (url.isLocalFile()) {
            paths << url.toLocalFile();
        }
    }
    m_queue.addFiles(paths);
}

void QuickTranscription::removeFile(const QString &path)
{
    m_queue.removeFile(path);
}

void QuickTranscription::setOption(const QString &id, const QVariant &value)
{
    if (id == QStringLiteral("profile")) {
        m_queue.chooseProfile(value.toString());
        return;
    }
    TranscribeOptions options = m_queue.options();
    if (id == QStringLiteral("service")) {
        if (options.speechProviderId != value.toString()) {
            options.speechModel.clear();
        }
        options.speechProviderId = value.toString();
    } else if (id == QStringLiteral("speechModel")) {
        options.speechModel = value.toString();
    } else if (id == QStringLiteral("vocabulary")) {
        options.applyVocabulary = value.toBool();
    } else if (id == QStringLiteral("refiner")) {
        if (options.refinementProviderId != value.toString()) {
            options.refinementModel.clear();
        }
        options.refinementProviderId = value.toString();
    } else if (id == QStringLiteral("refinerModel")) {
        options.refinementModel = value.toString();
    } else if (id == QStringLiteral("cleanup")) {
        options.cleanupStrength = value.toString();
    } else if (id == QStringLiteral("tone")) {
        options.tone = value.toString();
    } else if (id == QStringLiteral("language")) {
        options.spokenLanguage = value.toString();
    } else if (id == QStringLiteral("destination") && valueOf(kDestinations, value.toString())) {
        options.destination = *valueOf(kDestinations, value.toString());
    } else {
        qWarning().noquote() << "Transcription.setOption: no option" << id << "taking" << value;
        return;
    }
    m_queue.setOptions(options);
}

void QuickTranscription::setFolder(const QUrl &folder)
{
    TranscribeOptions options = m_queue.options();
    if (folder.isLocalFile()) {
        options.folder = folder.toLocalFile();
    } else if (options.folder.isEmpty()) {
        // One folder needs a folder; without one, back to beside each file.
        options.destination = TranscriptDestination::BesideInput;
    }
    m_queue.setOptions(options);
}

void QuickTranscription::start()
{
    m_queue.start();
}

void QuickTranscription::cancel()
{
    m_queue.cancel();
}

void QuickTranscription::reset()
{
    m_queue.backToSetup();
}

void QuickTranscription::retry(int index)
{
    m_queue.retry(index);
}

void QuickTranscription::copy(int index)
{
    if (index >= 0 && index < m_queue.results().size()) {
        QGuiApplication::clipboard()->setText(shownTranscript(m_queue.results().at(index), m_showRaw));
    }
}

void QuickTranscription::copyAll()
{
    QGuiApplication::clipboard()->setText(allTranscripts(m_queue.results(), m_showRaw));
}

QUrl QuickTranscription::exportFile(int index, const QString &format) const
{
    const std::optional<TranscriptFormat> chosen = valueOf(kFormats, format);
    if (!chosen || index < 0 || index >= m_queue.results().size()
        || !canExportAs(m_queue.results().at(index), *chosen)) {
        return {};
    }
    const QFileInfo audio(m_queue.results().at(index).path);
    return QUrl::fromLocalFile(
        audio.dir().filePath(audio.completeBaseName() + QStringLiteral("-transcribed.") + transcriptFileExtension(*chosen)));
}

void QuickTranscription::exportResult(int index, const QString &format, const QUrl &file)
{
    const std::optional<TranscriptFormat> chosen = valueOf(kFormats, format);
    if (!chosen || index < 0 || index >= m_queue.results().size()
        || !canExportAs(m_queue.results().at(index), *chosen) || !file.isLocalFile()) {
        qWarning().noquote() << "Transcription.exportResult: result" << index << "has no" << format << "to save in" << file;
        return;
    }
    const QString error =
        writeTranscript(file.toLocalFile(), exportedTranscript(m_queue.results().at(index), *chosen, m_showRaw));
    m_queue.setProblem(error);
    if (error.isEmpty()) {
        m_queue.markExported(index);
        emit exported(index);
    }
}

void QuickTranscription::exportAll(const QString &format, const QUrl &folder)
{
    const std::optional<TranscriptFormat> chosen = valueOf(kFormats, format);
    if (!chosen || !folder.isLocalFile()) {
        qWarning().noquote() << "Transcription.exportAll: no format" << format << "or folder" << folder;
        return;
    }
    QStringList errors;
    for (int index = 0; index < m_queue.results().size(); ++index) {
        const TranscribeFileResult &result = m_queue.results().at(index);
        if (!canExportAs(result, *chosen)) {
            continue;
        }
        QString error;
        const QString text = exportedTranscript(result, *chosen, m_showRaw);
        if (saveTranscript(result.path, folder.toLocalFile(), text, *chosen, &error).isEmpty()) {
            errors << error;
        } else {
            m_queue.markExported(index);
        }
    }
    m_queue.setProblem(errors.join(QLatin1Char('\n')));
    if (errors.isEmpty()) {
        emit exported(-1);
    }
}

} // namespace speecher
