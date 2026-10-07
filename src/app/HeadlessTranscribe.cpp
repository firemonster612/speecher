#include "app/HeadlessTranscribe.h"

#include "core/SettingsStore.h"
#include "core/Target.h"
#include "platform/audio/AudioPcmConverter.h"
#include "providers/ProviderRegistry.h"
#include "transcribe/TranscribePresentation.h"

#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTimer>

#include <algorithm>
#include <istream>
#include <optional>
#include <ostream>

namespace speecher {
namespace {

bool offers(const QList<ProviderDescriptor> &providers, const QString &id)
{
    return std::any_of(providers.cbegin(), providers.cend(),
                       [&id](const ProviderDescriptor &provider) { return provider.id == id; });
}

// The same seeding as the Transcribe page: the user's settings, with the
// profile's services, cleanup and tone underneath anything given explicitly.
TranscribeOptions resolveOptions(const HeadlessTranscribeOptions &options,
                                 AppSettings settings,
                                 const ProviderRegistry &providers)
{
    TranscribeOptions resolved;
    resolved.writingProfile = options.writingProfile.value_or(settings.refinement.defaultWritingProfile);
    const WritingProfileSettings profile = writingProfileSettingsFor(
        settings.refinement.writingProfiles, writingProfileFromName(resolved.writingProfile));
    if (options.spokenLanguage) {
        settings.speech.language = *options.spokenLanguage;
    }
    settings = providers.withProfileProviders(settings, profile);
    resolved.speechProviderId = options.speechProviderId.value_or(settings.speech.providerId);
    resolved.applyVocabulary = options.applyVocabulary;
    resolved.addedVocabulary = options.addedVocabulary;
    resolved.refinementProviderId = options.refinementProviderId.value_or(settings.refinement.providerId);
    resolved.cleanupStrength = options.cleanupStrength.value_or(profile.cleanupStrength);
    resolved.tone = options.tone.value_or(profile.tone);
    resolved.spokenLanguage = options.spokenLanguage;
    resolved.destination = options.destination;
    resolved.folder = options.folder;
    return resolved;
}

// Why a run cannot use the providers it resolved to, or empty when it can.
QString unofferedProviderError(const TranscribeOptions &resolved, ProviderRegistry *providers)
{
    if (!offers(providers->speechProviders(), resolved.speechProviderId)) {
        return QStringLiteral("Unknown speech provider: %1 (see speecher --help)").arg(resolved.speechProviderId);
    }
    if (resolved.refinementProviderId != QStringLiteral("none")
        && !offers(providers->refinementProviders(), resolved.refinementProviderId)) {
        return QStringLiteral("Unknown refinement provider: %1 (see speecher --help)")
            .arg(resolved.refinementProviderId);
    }
    return {};
}

// Copies in to a file named stdin in dir, because the decoder cannot probe a
// pipe. The file has no extension: the decoder probes its content, and the
// name is what progress shows and the saved transcript is named after.
// Returns its path, or empty with error set.
QString spoolStdin(std::istream &in, const QTemporaryDir &dir, QString *error)
{
    if (!dir.isValid()) {
        *error = QStringLiteral("Could not read stdin: %1").arg(dir.errorString());
        return {};
    }
    QFile file(dir.filePath(QStringLiteral("stdin")));
    if (!file.open(QIODevice::WriteOnly)) {
        *error = QStringLiteral("Could not read stdin: %1").arg(file.errorString());
        return {};
    }
    char buffer[64 * 1024];
    qint64 total = 0;
    while (in.read(buffer, sizeof buffer) || in.gcount() > 0) {
        if (file.write(buffer, in.gcount()) != in.gcount()) {
            *error = QStringLiteral("Could not read stdin: %1").arg(file.errorString());
            return {};
        }
        total += in.gcount();
    }
    if (in.bad()) {
        *error = QStringLiteral("Could not read stdin");
        return {};
    }
    if (total == 0) {
        *error = QStringLiteral("No audio on stdin");
        return {};
    }
    return file.fileName();
}

void writeJson(std::ostream &out, const QJsonObject &object)
{
    out << QJsonDocument(object).toJson(QJsonDocument::Compact).toStdString() << '\n';
    out.flush();
}

} // namespace

int runHeadlessTranscribe(const QStringList &files,
                          const HeadlessTranscribeOptions &options,
                          SettingsStore *settings,
                          ProviderRegistry *providers,
                          std::istream &in,
                          std::ostream &out,
                          std::ostream &err,
                          bool errIsTerminal)
{
    int failed = 0;
    // The summary closes every --json run, including one that never started.
    const auto finish = [&](int exitCode, const QString &error = {}) {
        if (!error.isEmpty()) {
            err << error.toStdString() << "\n";
        }
        if (options.json) {
            QJsonObject summary{{QStringLiteral("summary"), true},
                                {QStringLiteral("files"), int(files.size())},
                                {QStringLiteral("succeeded"), int(files.size()) - failed},
                                {QStringLiteral("failed"), failed}};
            if (!error.isEmpty()) {
                summary.insert(QStringLiteral("error"), error);
            }
            writeJson(out, summary);
        }
        return exitCode;
    };
    if (files.isEmpty()) {
        return finish(2, QStringLiteral("No audio files to transcribe"));
    }
    const TranscribeOptions resolved = resolveOptions(options, settings->snapshot(), *providers);
    if (const QString error = unofferedProviderError(resolved, providers); !error.isEmpty()) {
        return finish(2, error);
    }
    // Removes the spooled audio on every return.
    std::optional<QTemporaryDir> stdinDir;
    QString stdinPath;
    if (files == QStringList{kStdinFile}) {
        stdinDir.emplace();
        QString error;
        stdinPath = spoolStdin(in, *stdinDir, &error);
        if (stdinPath.isEmpty()) {
            failed = 1;
            return finish(1, error);
        }
    }
    const QStringList inputs = stdinPath.isEmpty() ? files : QStringList{stdinPath};
    // Saving happens here rather than in the session, so --raw can save what
    // it prints.
    TranscribeOptions sessionOptions = resolved;
    sessionOptions.destination = TranscriptDestination::None;
    // Subtitles come from the timed Raw Transcript, so refining would be a
    // call whose text nobody sees.
    if (options.format != TranscriptFormat::Text) {
        sessionOptions.refinementProviderId = QStringLiteral("none");
    }
    const bool refines = refinesTranscripts(sessionOptions, settings->snapshot().refinement);
    const QString speechProvider = batchLabels(resolved, *providers, settings->snapshot().refinement).speech;

    FileTranscriptionSession session(settings, providers);
    QEventLoop loop;
    QElapsedTimer phaseClock;
    TranscribePhase phase = TranscribePhase::Reading;
    qreal fractionSent = 0.0;
    ForwardProgress shownProgress;
    int lastPercent = -1;
    QString name;
    // A new phase always gets a line; within one, a terminal gets every
    // percent and a log every tenth.
    const auto showProgress = [&](bool newPhase) {
        const int percent = int(
            shownProgress.advance(overallFileProgress(fractionSent, phase, refines, phaseClock.elapsed())) * 100);
        const std::string line = (name + QStringLiteral(": ") + transcribePhaseLabel(phase)
                                  + QStringLiteral(" %1%").arg(percent))
                                     .toStdString();
        if (errIsTerminal) {
            if (newPhase || percent != lastPercent) {
                err << "\r\033[K" << line << std::flush;
            }
        } else if (newPhase || percent / 10 != lastPercent / 10) {
            err << line << "\n" << std::flush;
        }
        lastPercent = percent;
    };
    const auto setPhase = [&](TranscribePhase next) {
        phase = next;
        phaseClock.start();
        showProgress(true);
    };
    QObject::connect(&session, &FileTranscriptionSession::fileStarted, &loop, [&](int, const QString &path) {
        name = QFileInfo(path).fileName();
        fractionSent = 0.0;
        shownProgress = {};
        setPhase(TranscribePhase::Reading);
    });
    QObject::connect(&session, &FileTranscriptionSession::fileDecoded, &loop,
                     [&] { setPhase(TranscribePhase::Transcribing); });
    QObject::connect(&session, &FileTranscriptionSession::fileProgress, &loop, [&](int, qreal fraction) {
        fractionSent = fraction;
        if (fraction >= 1.0) {
            setPhase(TranscribePhase::Finishing);
        } else {
            showProgress(false);
        }
    });
    QObject::connect(&session, &FileTranscriptionSession::fileRefining, &loop,
                     [&] { setPhase(TranscribePhase::Refining); });

    QObject::connect(&session, &FileTranscriptionSession::fileFinished, &loop,
                     [&](int, TranscribeFileResult result) {
                         if (errIsTerminal) {
                             err << "\r\033[K";
                         }
                         // Without timings there are no subtitles, so the file
                         // fails; its JSON still carries the text, as a failed
                         // file's does.
                         const bool exportable = canExportAs(result, options.format);
                         if (!result.failed() && !exportable) {
                             result.error = subtitlesNeedTimings(speechProvider);
                         }
                         const QString text = exportable ? exportedTranscript(result, options.format, options.raw)
                                                         : shownTranscript(result, options.raw);
                         // A transcript that was asked to be saved and was not
                         // fails the file, though it still prints.
                         bool ok = exportable;
                         if (ok && resolved.destination != TranscriptDestination::None) {
                             const QString folder = resolved.destination == TranscriptDestination::Folder
                                 ? resolved.folder
                                 : QFileInfo(result.path).absolutePath();
                             QString error;
                             result.savedPath = saveTranscript(result.path, folder, text, options.format, &error);
                             if (!error.isEmpty()) {
                                 result.error = error;
                                 ok = false;
                             }
                         }
                         if (!ok) {
                             ++failed;
                             err << name.toStdString() << ": failed: " << result.error.toStdString() << "\n";
                         } else {
                             if (!result.error.isEmpty()) {
                                 err << name.toStdString() << ": " << result.error.toStdString() << "\n";
                             }
                             err << name.toStdString() << ": "
                                 << (result.savedPath.isEmpty()
                                         ? std::string("done")
                                         : "saved " + QDir::toNativeSeparators(result.savedPath).toStdString())
                                 << "\n";
                         }
                         err.flush();
                         if (options.json) {
                             QJsonObject object{{QStringLiteral("file"),
                                                 result.path == stdinPath ? kStdinFile : result.path},
                                                {QStringLiteral("ok"), ok},
                                                {QStringLiteral("text"), text}};
                             if (!result.savedPath.isEmpty()) {
                                 object.insert(QStringLiteral("saved"), result.savedPath);
                             }
                             if (!result.error.isEmpty()) {
                                 object.insert(QStringLiteral("error"), result.error);
                             }
                             writeJson(out, object);
                         } else if (options.printTranscripts && exportable) {
                             if (files.size() > 1) {
                                 out << "# " << name.toStdString() << "\n\n";
                             }
                             out << text.toStdString() << "\n" << (files.size() > 1 ? "\n" : "");
                             out.flush();
                         }
                         // No progress line until the next file starts.
                         name.clear();
                     });
    QObject::connect(&session, &FileTranscriptionSession::batchFinished, &loop, [&] { loop.quit(); });

    // The waits between engine signals still move the line forward.
    QTimer tick;
    tick.setInterval(250);
    QObject::connect(&tick, &QTimer::timeout, &loop, [&] {
        if (!name.isEmpty()) {
            showProgress(false);
        }
    });
    tick.start();
    // The session is this run's own and the files are there, so a refusal
    // would mean a batch already under way.
    if (!session.start(inputs, sessionOptions)) {
        return finish(1, QStringLiteral("Could not start: a transcription is already running"));
    }
    if (session.isRunning()) {
        loop.exec();
    }
    return finish(failed > 0 ? 1 : 0);
}

int runHeadlessListen(const HeadlessTranscribeOptions &options,
                      std::optional<int> untilSilenceMs,
                      AudioInput *microphone,
                      const std::function<bool()> &stopRequested,
                      bool enterStops,
                      SettingsStore *settings,
                      ProviderRegistry *providers,
                      std::ostream &out,
                      std::ostream &err)
{
    // Every --json run prints one object, including one that never started.
    const auto finish = [&](int exitCode, const QString &text, const QString &error) {
        if (!error.isEmpty()) {
            err << error.toStdString() << "\n";
        }
        if (options.json) {
            QJsonObject object{{QStringLiteral("ok"), exitCode == 0}, {QStringLiteral("text"), text}};
            if (!error.isEmpty()) {
                object.insert(QStringLiteral("error"), error);
            }
            writeJson(out, object);
        } else if (exitCode == 0) {
            out << text.toStdString() << "\n";
            out.flush();
        }
        return exitCode;
    };
    const TranscribeOptions resolved = resolveOptions(options, settings->snapshot(), *providers);
    if (const QString error = unofferedProviderError(resolved, providers); !error.isEmpty()) {
        return finish(2, {}, error);
    }
    if (!microphone) {
        return finish(1, {},
                      QStringLiteral("Microphone access is off for this terminal. Allow it under Privacy & Security "
                                     "> Microphone, then try again."));
    }

    FileTranscriptionSession session(settings, providers);
    QEventLoop loop;
    TranscribeFileResult result;
    QObject::connect(&session, &FileTranscriptionSession::fileRefining, &loop, [&] {
        err << transcribePhaseLabel(TranscribePhase::Refining).toStdString() << "\n" << std::flush;
    });
    QObject::connect(&session, &FileTranscriptionSession::fileFinished, &loop,
                     [&](int, const TranscribeFileResult &finished) { result = finished; });
    QObject::connect(&session, &FileTranscriptionSession::batchFinished, &loop, [&] { loop.quit(); });

    // Speech by Skip silence's threshold restarts the clock, which is not
    // running until the first.
    const int voiceThreshold = settings->audioCaptureSettings().vadThresholdPercent;
    QElapsedTimer sinceSpeech;
    QObject::connect(microphone, &AudioInput::audioChunk, &loop, [&](const QByteArray &pcm) {
        if (isVoiced(rmsForPcm16(pcm), voiceThreshold)) {
            sinceSpeech.start();
        }
    });
    QTimer poll;
    poll.setInterval(100);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        const bool silent = untilSilenceMs && sinceSpeech.isValid() && sinceSpeech.elapsed() >= *untilSilenceMs;
        if (!silent && !stopRequested()) {
            return;
        }
        poll.stop();
        err << transcribePhaseLabel(TranscribePhase::Finishing).toStdString() << "\n" << std::flush;
        session.finishListening();
    });

    // Returns once the microphone is open, or the run has failed.
    session.startListening(microphone, resolved);
    if (session.isRunning()) {
        const QString stopKeys = enterStops ? QStringLiteral("Enter or Ctrl-C") : QStringLiteral("Ctrl-C");
        err << (untilSilenceMs ? QStringLiteral("Listening. Press %1 to stop, or pause for %2 s.")
                                     .arg(stopKeys)
                                     .arg(*untilSilenceMs / 1000.0)
                               : QStringLiteral("Listening. Press %1 to stop.").arg(stopKeys))
                   .toStdString()
            << "\n"
            << std::flush;
        poll.start();
        loop.exec();
    }
    return finish(result.failed() ? 1 : 0, shownTranscript(result, options.raw), result.error);
}

} // namespace speecher
