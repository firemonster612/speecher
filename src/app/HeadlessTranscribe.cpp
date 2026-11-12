#include "app/HeadlessTranscribe.h"

#include "core/ProviderChain.h"
#include "core/ProviderTable.h"
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
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTimer>

#include <algorithm>
#include <atomic>
#include <istream>
#include <optional>
#include <ostream>
#include <string>
#ifdef Q_OS_WIN
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace speecher {
namespace {

// What the spooled stdin is named: progress shows it and the saved transcript
// is named after it.
const QString kSpoolName = QStringLiteral("stdin");

// The spooled file and its folder while a run has them, as native strings
// built beforehand so removeStdinSpool need not allocate.
#ifdef Q_OS_WIN
std::wstring g_spoolFile;
std::wstring g_spoolFolder;
#else
std::string g_spoolFile;
std::string g_spoolFolder;
#endif
std::atomic<bool> g_spooling{false};

bool offers(const QList<ProviderDescriptor> &providers, const QString &id)
{
    return std::any_of(providers.cbegin(), providers.cend(),
                       [&id](const ProviderDescriptor &provider) { return provider.id == id; });
}

// The refiner a headless run starts with, or kNoRefinementProvider: the one
// the command line names, as named, so one this build lacks is refused rather
// than skipped, otherwise the first its Service Order runs.
QString firstRefiner(const DictationPlan &plan, const RunOverrides &overrides)
{
    if (overrides.refinement) {
        return overrides.refinement->service;
    }
    return serviceChain(plan.settings.refinement).value(0, kNoRefinementProvider);
}

// Why a run cannot use the providers it planned, or empty when it can.
QString unofferedProviderError(const DictationPlan &plan, const RunOverrides &overrides, ProviderRegistry *providers)
{
    const QString speech = plan.settings.speech.services.main();
    if (!offers(providers->speechProviders(), speech)) {
        return QStringLiteral("Unknown speech provider: %1 (see speecher --help)").arg(speech);
    }
    const QString refiner = firstRefiner(plan, overrides);
    if (refiner != kNoRefinementProvider && !offers(providers->refinementProviders(), refiner)) {
        return QStringLiteral("Unknown refinement provider: %1 (see speecher --help)").arg(refiner);
    }
    return {};
}

void rememberSpool(const QTemporaryDir &dir)
{
#ifdef Q_OS_WIN
    g_spoolFile = QDir::toNativeSeparators(dir.filePath(kSpoolName)).toStdWString();
    g_spoolFolder = QDir::toNativeSeparators(dir.path()).toStdWString();
#else
    g_spoolFile = QFile::encodeName(dir.filePath(kSpoolName)).toStdString();
    g_spoolFolder = QFile::encodeName(dir.path()).toStdString();
#endif
    g_spooling = true;
}

// Opens file to write. On Windows it is shared for deletion, so
// removeStdinSpool can delete it while stdin is still being read. Returns why
// it could not, or empty.
QString openSpool(QFile &file)
{
#ifdef Q_OS_WIN
    const HANDLE handle =
        CreateFileW(reinterpret_cast<const wchar_t *>(QDir::toNativeSeparators(file.fileName()).utf16()),
                    GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, CREATE_NEW,
                    FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return qt_error_string(int(GetLastError()));
    }
    const int descriptor = _open_osfhandle(reinterpret_cast<intptr_t>(handle), _O_WRONLY | _O_BINARY);
    if (descriptor == -1) {
        CloseHandle(handle);
        return QStringLiteral("Could not open the temporary file");
    }
    if (!file.open(descriptor, QIODevice::WriteOnly, QFileDevice::AutoCloseHandle)) {
        _close(descriptor);
        return file.errorString();
    }
    return {};
#else
    return file.open(QIODevice::WriteOnly) ? QString() : file.errorString();
#endif
}

// Copies in to a file named kSpoolName in dir, because the decoder cannot
// probe a pipe. The file has no extension: the decoder probes its content.
// Returns its path, or empty with error set.
QString spoolStdin(std::istream &in, const QTemporaryDir &dir, QString *error)
{
    if (!dir.isValid()) {
        *error = QStringLiteral("Could not read stdin: %1").arg(dir.errorString());
        return {};
    }
    const QString path = dir.filePath(kSpoolName);
    QFile file(path);
    if (const QString reason = openSpool(file); !reason.isEmpty()) {
        *error = QStringLiteral("Could not read stdin: %1").arg(reason);
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
    // The last of it is written only now, so a full disk can fail here.
    if (!file.flush()) {
        *error = QStringLiteral("Could not read stdin: %1").arg(file.errorString());
        return {};
    }
    if (in.bad()) {
        *error = QStringLiteral("Could not read stdin");
        return {};
    }
    if (total == 0) {
        *error = QStringLiteral("No audio on stdin");
        return {};
    }
    return path;
}

void writeJson(std::ostream &out, const QJsonObject &object)
{
    out << QJsonDocument(object).toJson(QJsonDocument::Compact).toStdString() << '\n';
    out.flush();
}

// A run whose output stdout did not take, as on a full disk or for a reader
// that quit, lost it, so it fails: Qt ignores SIGPIPE, so nothing else says
// so. exitCode is the run's otherwise.
int checkedExitCode(int exitCode, std::ostream &out, std::ostream &err)
{
    if (out) {
        return exitCode;
    }
    err << "Could not write to stdout\n";
    return std::max(exitCode, 1);
}

} // namespace

void removeStdinSpool()
{
    if (!g_spooling) {
        return;
    }
#ifdef Q_OS_WIN
    // Best effort: while decoding, FFmpeg holds the file without sharing it
    // for deletion, and a file only marked deleted can keep the folder until
    // the process ends.
    DeleteFileW(g_spoolFile.c_str());
    RemoveDirectoryW(g_spoolFolder.c_str());
#else
    unlink(g_spoolFile.c_str());
    rmdir(g_spoolFolder.c_str());
#endif
}

int runHeadlessTranscribe(const QStringList &files,
                          const RunOverrides &overrides,
                          const HeadlessTranscribeOptions &options,
                          SettingsStore *settings,
                          ProviderRegistry *providers,
                          std::istream &in,
                          std::ostream &out,
                          std::ostream &err,
                          bool errIsTerminal)
{
    int succeeded = 0;
    int failed = 0;
    // The summary closes every --json run, including one that never started.
    const auto finish = [&](int exitCode, const QString &error = {}) {
        if (!error.isEmpty()) {
            err << error.toStdString() << "\n";
        }
        if (options.json) {
            QJsonObject summary{{QStringLiteral("summary"), true},
                                {QStringLiteral("files"), int(files.size())},
                                {QStringLiteral("succeeded"), succeeded},
                                {QStringLiteral("failed"), failed}};
            if (!error.isEmpty()) {
                summary.insert(QStringLiteral("error"), error);
            }
            writeJson(out, summary);
        }
        return checkedExitCode(exitCode, out, err);
    };
    if (files.isEmpty()) {
        return finish(2, QStringLiteral("No audio files to transcribe"));
    }
    const AppSettings saved = settings->dictationSnapshot();
    if (const QString error = unofferedProviderError(
            planDictation(saved, Target{}, overrides, providers->offersSpeech()), overrides, providers);
        !error.isEmpty()) {
        return finish(2, error);
    }
    // Subtitles come from the timed Raw Transcript, so refining would be a
    // call whose text nobody sees.
    RunOverrides run = overrides;
    if (options.format != TranscriptFormat::Text) {
        run.refinement = ServiceChoice{kNoRefinementProvider};
    }
    const DictationPlan plan = planDictation(saved, Target{}, run, providers->offersSpeech());
    // Removes the spooled audio on every return.
    std::optional<QTemporaryDir> stdinDir;
    // Declared after it, so it runs first.
    const auto forgetSpool = qScopeGuard([] { g_spooling = false; });
    QString stdinPath;
    if (files == QStringList{kStdinFile}) {
        stdinDir.emplace();
        if (stdinDir->isValid()) {
            rememberSpool(*stdinDir);
        }
        // The length is unknown until stdin ends, so this has no percent.
        err << (errIsTerminal ? "\r\033[K" : "") << kSpoolName.toStdString() << ": "
            << transcribePhaseLabel(TranscribePhase::Reading).toStdString() << (errIsTerminal ? "" : "\n")
            << std::flush;
        QString error;
        stdinPath = spoolStdin(in, *stdinDir, &error);
        if (errIsTerminal) {
            err << "\r\033[K";
        }
        if (stdinPath.isEmpty()) {
            failed = 1;
            return finish(1, error);
        }
    }
    const QStringList inputs = stdinPath.isEmpty() ? files : QStringList{stdinPath};
    const bool refines = refinesTranscripts(plan);
    const QString speechProvider = batchLabels(plan, *providers).speech;

    FileTranscriptionSession session(settings, providers);
    QEventLoop loop;
    QElapsedTimer phaseClock;
    TranscribePhase phase = TranscribePhase::Reading;
    qreal fractionSent = 0.0;
    ForwardProgress shownProgress;
    int lastPercent = -1;
    QString name;
    // A log has stdin's Reading line already, from the spooling.
    const bool readingLogged = !stdinPath.isEmpty() && !errIsTerminal;
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
        if (readingLogged) {
            phaseClock.start();
            return;
        }
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
                         if (ok && options.destination != TranscriptDestination::None) {
                             const QString folder = options.destination == TranscriptDestination::Folder
                                 ? options.folder
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
                             ++succeeded;
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
    // would mean a batch already under way. It saves nothing: saving happens
    // here, so --raw can save what it prints.
    if (!session.start(inputs, run, {TranscriptDestination::None})) {
        return finish(1, QStringLiteral("Could not start: a transcription is already running"));
    }
    if (session.isRunning()) {
        loop.exec();
    }
    return finish(failed > 0 ? 1 : 0);
}

int runHeadlessListen(const RunOverrides &overrides,
                      const HeadlessTranscribeOptions &options,
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
        return checkedExitCode(exitCode, out, err);
    };
    const DictationPlan plan =
        planDictation(settings->dictationSnapshot(), Target{}, overrides, providers->offersSpeech());
    if (const QString error = unofferedProviderError(plan, overrides, providers); !error.isEmpty()) {
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
    const int voiceThreshold = settings->dictationSnapshot().audio.vadThresholdPercent;
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

    // Listening once the microphone is open; a microphone that will not open
    // fails the run instead.
    QObject::connect(microphone, &AudioInput::started, &loop, [&] {
        const QString stopKeys = enterStops ? QStringLiteral("Enter or Ctrl-C") : QStringLiteral("Ctrl-C");
        err << (untilSilenceMs ? QStringLiteral("Listening. Press %1 to stop, or pause for %2 s.")
                                     .arg(stopKeys)
                                     .arg(*untilSilenceMs / 1000.0)
                               : QStringLiteral("Listening. Press %1 to stop.").arg(stopKeys))
                   .toStdString()
            << "\n"
            << std::flush;
        poll.start();
    }, Qt::SingleShotConnection);
    session.startListening(microphone, overrides);
    if (session.isRunning()) {
        loop.exec();
    }
    return finish(result.failed() ? 1 : 0, shownTranscript(result, options.raw), result.error);
}

} // namespace speecher
