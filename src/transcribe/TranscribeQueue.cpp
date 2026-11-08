#include "transcribe/TranscribeQueue.h"

#include "core/ProviderChain.h"
#include "core/SettingsStore.h"
#include "providers/ProviderRegistry.h"

#include <QDir>
#include <QFileInfo>
#include <QMimeDatabase>

#include <algorithm>
#include <utility>

namespace speecher {

namespace {

bool cleanupLevelOffered(const QString &level, const QList<CustomCleanupLevel> &custom)
{
    return kBuiltInCleanupLevels.contains(level)
        || std::any_of(custom.cbegin(), custom.cend(), [&level](const CustomCleanupLevel &c) { return c.id == level; });
}

bool toneOffered(const QString &tone, const QList<CustomTone> &custom)
{
    return kBuiltInTones.contains(tone)
        || std::any_of(custom.cbegin(), custom.cend(), [&tone](const CustomTone &c) { return c.id == tone; });
}

} // namespace

TranscribeQueue::TranscribeQueue(SettingsStore *settings,
                                 ProviderRegistry *providers,
                                 FileTranscriptionSession *session,
                                 Refusal refusal,
                                 QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_providers(providers)
    , m_session(session)
    , m_refusal(std::move(refusal))
{
    // What the batch would run with follows the settings.
    connect(settings, &SettingsStore::snapshotApplied, this, [this] {
        dropWithdrawnChoices();
        emit optionsChanged();
    });
    connect(session, &FileTranscriptionSession::fileStarted, this, [this](int index) {
        if (!followsBatch()) {
            return;
        }
        m_current = index;
        resetFile();
        enter(TranscribePhase::Reading);
    });
    connect(session, &FileTranscriptionSession::fileDecoded, this,
            [this](int index, const QVector<float> &peaks, qint64 durationMs) {
                // A retry's file is its result's.
                m_listed[m_retrying >= 0 ? m_results.value(m_retrying).path : m_batch.value(index)].durationMs =
                    durationMs;
                if (!followsBatch()) {
                    return;
                }
                m_peaks = peaks;
                enter(TranscribePhase::Transcribing);
            });
    connect(session, &FileTranscriptionSession::fileProgress, this, [this](int, qreal fraction) {
        if (!followsBatch()) {
            return;
        }
        m_fractionSent = fraction;
        if (fraction >= 1.0 && m_phase == TranscribePhase::Transcribing) {
            enter(TranscribePhase::Finishing);
        }
    });
    connect(session, &FileTranscriptionSession::filePartialText, this, [this](int, const QString &text) {
        if (!followsBatch()) {
            return;
        }
        m_partial = text;
        emit partialChanged();
    });
    connect(session, &FileTranscriptionSession::fileRefining, this, [this] {
        if (followsBatch()) {
            enter(TranscribePhase::Refining);
        }
    });
    connect(session, &FileTranscriptionSession::fileFinished, this, [this](int, const TranscribeFileResult &result) {
        if (!followsBatch()) {
            return;
        }
        m_fileDone = true;
        m_results.append(result);
        emit fileChanged();
        emit resultsChanged();
    });
    connect(session, &FileTranscriptionSession::batchFinished, this,
            [this](const QList<TranscribeFileResult> &results, bool cancelled) {
                if (m_retrying >= 0) {
                    if (!results.isEmpty() && m_retrying < m_results.size()) {
                        m_results[m_retrying] = results.first();
                    }
                    m_retrying = -1;
                    emit resultsChanged();
                    emit unsavedResultsChanged();
                    return;
                }
                if (m_step != Step::Transcribe) {
                    return;
                }
                m_current = -1;
                m_cancelled = cancelled;
                // A batch cancelled before any file finished has nothing to show.
                setStep(m_results.isEmpty() ? Step::Configure : Step::Export);
            });
}

TranscribeQueue::Step TranscribeQueue::step() const
{
    return m_step;
}

const QStringList &TranscribeQueue::files() const
{
    return m_files;
}

void TranscribeQueue::addFiles(const QStringList &paths)
{
    // Files opened while a batch runs wait in the setup list it returns to.
    if (m_step == Step::Export) {
        backToSetup();
    }
    for (const QString &path : paths) {
        const QString absolute = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
        if (!isAudioFile(absolute) || m_files.contains(absolute)) {
            continue;
        }
        m_files << absolute;
        const QFileInfo info(absolute);
        m_listed.insert(absolute,
                        {QMimeDatabase().mimeTypeForFile(info).name().startsWith(QStringLiteral("video/")), info.size()});
        probeAudioDuration(absolute, this, [this, absolute](qint64 durationMs) {
            m_listed[absolute].durationMs = durationMs;
            emit filesChanged();
        });
    }
    emit filesChanged();
}

void TranscribeQueue::removeFile(const QString &path)
{
    if (m_files.removeOne(path)) {
        emit filesChanged();
    }
}

TranscribeQueue::ListedFile TranscribeQueue::listedFile(const QString &path) const
{
    return m_listed.value(path);
}

const RunOverrides &TranscribeQueue::overrides() const
{
    return m_overrides;
}

void TranscribeQueue::setOverrides(const RunOverrides &overrides)
{
    m_overrides = overrides;
    emit optionsChanged();
}

const TranscribeOptions &TranscribeQueue::options() const
{
    return m_options;
}

void TranscribeQueue::setOptions(const TranscribeOptions &options)
{
    m_options = options;
    emit optionsChanged();
}

// The page reads this on the GUI thread as its bindings change, so it never
// waits for the keyring: a model name is no secret.
DictationPlan TranscribeQueue::plan(const RunOverrides &overrides) const
{
    return planDictation(m_settings->dictationSnapshot(), Target{}, overrides, m_providers->offersSpeech());
}

bool TranscribeQueue::start()
{
    // Refused before anything changes, so a batch or retry still running
    // keeps the list, results and choices it reports into.
    if (m_files.isEmpty()) {
        return false;
    }
    const QString refusal = m_refusal();
    if (!refusal.isEmpty()) {
        setProblem(refusal);
        return false;
    }
    // Nothing from here to the session's start waits for the keyring, so no
    // event loop runs where a dictation could start.
    const DictationPlan batchPlan = plan(m_overrides);
    m_batch = m_files;
    m_batchOverrides = m_overrides;
    m_batchOptions = m_options;
    // Pinned, so a retry hears in this batch's language after the setting
    // changes.
    m_batchOverrides.spokenLanguage = batchPlan.settings.speech.language;
    m_batchLabels = speecher::batchLabels(batchPlan, *m_providers);
    m_batchRefines = refinesTranscripts(batchPlan);
    m_results.clear();
    m_exported.clear();
    m_cancelled = false;
    m_current = -1;
    resetFile();
    setProblem({});
    emit resultsChanged();
    // Before the session starts: a batch whose files all fail at once
    // finishes inside it.
    setStep(Step::Transcribe);
    // Checked again: what the signals above reach may have started a
    // dictation.
    const QString lateRefusal = m_refusal();
    if (!lateRefusal.isEmpty() || !m_session->start(m_batch, m_batchOverrides, m_batchOptions)) {
        setStep(Step::Configure);
        setProblem(lateRefusal);
        return false;
    }
    return true;
}

void TranscribeQueue::cancel()
{
    m_session->cancel();
}

// The finished files leave the list; files added meanwhile, and any a
// cancelled batch never reached, stay.
void TranscribeQueue::backToSetup()
{
    // A retry left running would report into results no longer shown, and
    // hold up the next batch.
    if (m_retrying >= 0) {
        cancel();
    }
    for (const TranscribeFileResult &result : std::as_const(m_results)) {
        m_files.removeOne(result.path);
    }
    // The folder stays, for the next time one folder is chosen.
    m_overrides = {};
    m_options = {TranscriptDestination::BesideInput, m_options.folder};
    emit optionsChanged();
    setProblem({});
    emit filesChanged();
    setStep(Step::Configure);
}

const QStringList &TranscribeQueue::batch() const
{
    return m_batch;
}

const RunOverrides &TranscribeQueue::batchOverrides() const
{
    return m_batchOverrides;
}

const TranscribeOptions &TranscribeQueue::batchOptions() const
{
    return m_batchOptions;
}

const TranscribeBatchLabels &TranscribeQueue::batchLabels() const
{
    return m_batchLabels;
}

bool TranscribeQueue::batchRefines() const
{
    return m_batchRefines;
}

int TranscribeQueue::current() const
{
    return m_current;
}

TranscribeQueue::FileState TranscribeQueue::fileState(int index) const
{
    if (index < m_results.size()) {
        return m_results.at(index).failed() ? FileState::Failed : FileState::Done;
    }
    return index == m_current ? FileState::Current : FileState::Waiting;
}

TranscribePhase TranscribeQueue::phase() const
{
    return m_phase;
}

const QVector<float> &TranscribeQueue::peaks() const
{
    return m_peaks;
}

const QString &TranscribeQueue::partialText() const
{
    return m_partial;
}

qreal TranscribeQueue::progress()
{
    if (m_fileDone) {
        return 1.0;
    }
    return m_progress.advance(overallFileProgress(m_fractionSent, m_phase, m_batchRefines, m_phaseClock.elapsed()));
}

const QList<TranscribeFileResult> &TranscribeQueue::results() const
{
    return m_results;
}

bool TranscribeQueue::cancelled() const
{
    return m_cancelled;
}

void TranscribeQueue::retry(int index)
{
    if (m_retrying >= 0 || index < 0 || index >= m_results.size() || !m_results.at(index).failed()) {
        return;
    }
    m_retrying = index;
    const QString refusal = m_refusal();
    if (!refusal.isEmpty() || !m_session->start({m_results.at(index).path}, m_batchOverrides, m_batchOptions)) {
        m_retrying = -1;
        setProblem(refusal);
        return;
    }
    setProblem({});
    emit resultsChanged();
}

int TranscribeQueue::retrying() const
{
    return m_retrying;
}

const QString &TranscribeQueue::problem() const
{
    return m_problem;
}

void TranscribeQueue::setProblem(const QString &problem)
{
    if (problem != m_problem) {
        m_problem = problem;
        emit problemChanged();
    }
}

void TranscribeQueue::markExported(int index)
{
    m_exported.insert(index);
    emit unsavedResultsChanged();
}

bool TranscribeQueue::holdsUnsavedResults() const
{
    if (m_retrying >= 0) {
        return true;
    }
    // Back at setup, the results were dismissed.
    if (m_step == Step::Configure) {
        return false;
    }
    for (qsizetype i = 0; i < m_results.size(); ++i) {
        const TranscribeFileResult &result = m_results.at(i);
        if (!result.failed() && result.savedPath.isEmpty() && !m_exported.contains(int(i))) {
            return true;
        }
    }
    return false;
}

// A profile, cleanup level or tone deleted since it was chosen gives way to
// what the settings give, as it would for dictation.
void TranscribeQueue::dropWithdrawnChoices()
{
    const RefinementSettings refinement = m_settings->dictationSnapshot().refinement;
    const std::optional<QString> &profile = m_overrides.writingProfile;
    if (profile && offeredWritingProfile(*profile, refinement.writingProfiles, {}).isEmpty()) {
        m_overrides.writingProfile.reset();
    }
    if (m_overrides.cleanupLevel && !cleanupLevelOffered(*m_overrides.cleanupLevel, refinement.customCleanupLevels)) {
        m_overrides.cleanupLevel.reset();
    }
    if (m_overrides.tone && !toneOffered(*m_overrides.tone, refinement.customTones)) {
        m_overrides.tone.reset();
    }
}

void TranscribeQueue::setStep(Step step)
{
    if (step != m_step) {
        m_step = step;
        emit stepChanged();
        emit unsavedResultsChanged();
    }
}

void TranscribeQueue::resetFile()
{
    m_phase = TranscribePhase::Reading;
    m_phaseClock.start();
    m_fractionSent = 0.0;
    m_progress = {};
    m_fileDone = false;
    m_peaks.clear();
    m_partial.clear();
    emit partialChanged();
}

void TranscribeQueue::enter(TranscribePhase phase)
{
    m_phase = phase;
    m_phaseClock.start();
    emit fileChanged();
}

bool TranscribeQueue::followsBatch() const
{
    return m_step == Step::Transcribe && m_retrying < 0;
}

} // namespace speecher
