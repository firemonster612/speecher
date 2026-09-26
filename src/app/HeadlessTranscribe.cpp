#include "app/HeadlessTranscribe.h"

#include "core/SettingsStore.h"
#include "core/Target.h"
#include "providers/ProviderRegistry.h"
#include "transcribe/TranscribePresentation.h"

#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <ostream>

namespace speecher {
namespace {

bool offers(const QList<ProviderDescriptor> &providers, const QString &id)
{
    return std::any_of(providers.cbegin(), providers.cend(),
                       [&id](const ProviderDescriptor &provider) { return provider.id == id; });
}

// The same seeding as the Transcribe page: the user's settings, with the
// profile's cleanup and tone underneath anything given explicitly.
TranscribeOptions resolveOptions(const HeadlessTranscribeOptions &options, const AppSettings &settings)
{
    TranscribeOptions resolved;
    resolved.speechProviderId = options.speechProviderId.value_or(settings.speech.providerId);
    resolved.applyVocabulary = options.applyVocabulary;
    resolved.refinementProviderId = options.refinementProviderId.value_or(settings.refinement.providerId);
    resolved.writingProfile = options.writingProfile.value_or(settings.refinement.defaultWritingProfile);
    const WritingProfileSettings profile = writingProfileSettingsFor(
        settings.refinement.writingProfiles, writingProfileFromName(resolved.writingProfile));
    resolved.cleanupStrength = options.cleanupStrength.value_or(profile.cleanupStrength);
    resolved.tone = options.tone.value_or(profile.tone);
    resolved.destination = options.destination;
    resolved.folder = options.folder;
    return resolved;
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
                          std::ostream &out,
                          std::ostream &err,
                          bool errIsTerminal)
{
    const TranscribeOptions resolved = resolveOptions(options, settings->snapshot());
    if (!offers(providers->speechProviders(), resolved.speechProviderId)) {
        err << "Unknown speech provider: " << resolved.speechProviderId.toStdString() << "\n";
        return 2;
    }
    if (resolved.refinementProviderId != QStringLiteral("none")
        && !offers(providers->refinementProviders(), resolved.refinementProviderId)) {
        err << "Unknown refinement provider: " << resolved.refinementProviderId.toStdString() << "\n";
        return 2;
    }
    const bool refines = refinesTranscripts(resolved);
    // Saving happens here rather than in the session, so --raw can save what
    // it prints.
    TranscribeOptions sessionOptions = resolved;
    sessionOptions.destination = TranscriptDestination::None;

    FileTranscriptionSession session(settings, providers);
    QEventLoop loop;
    QElapsedTimer phaseClock;
    TranscribePhase phase = TranscribePhase::Reading;
    qreal fractionSent = 0.0;
    int lastPercent = -1;
    QString name;
    // A new phase always gets a line; within one, a terminal gets every
    // percent and a log every tenth.
    const auto showProgress = [&](bool newPhase) {
        const int percent = int(overallFileProgress(fractionSent, phase, refines, phaseClock.elapsed()) * 100);
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

    int failed = 0;
    QObject::connect(&session, &FileTranscriptionSession::fileFinished, &loop,
                     [&](int, TranscribeFileResult result) {
                         if (errIsTerminal) {
                             err << "\r\033[K";
                         }
                         const QString text = shownTranscript(result, options.raw);
                         if (!result.failed() && resolved.destination != TranscriptDestination::None) {
                             const QString folder = resolved.destination == TranscriptDestination::Folder
                                 ? resolved.folder
                                 : QFileInfo(result.path).absolutePath();
                             QString error;
                             result.savedPath = saveTranscript(result.path, folder, text, &error);
                             if (!error.isEmpty()) {
                                 result.error = error;
                             }
                         }
                         if (result.failed()) {
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
                             QJsonObject object{{QStringLiteral("file"), result.path},
                                                {QStringLiteral("ok"), !result.failed()},
                                                {QStringLiteral("text"), text}};
                             if (!result.savedPath.isEmpty()) {
                                 object.insert(QStringLiteral("saved"), result.savedPath);
                             }
                             if (!result.error.isEmpty()) {
                                 object.insert(QStringLiteral("error"), result.error);
                             }
                             writeJson(out, object);
                         } else if (options.printTranscripts && !result.failed()) {
                             if (files.size() > 1) {
                                 out << "# " << name.toStdString() << "\n\n";
                             }
                             out << text.toStdString() << "\n" << (files.size() > 1 ? "\n" : "");
                             out.flush();
                         }
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
    if (!session.start(files, sessionOptions)) {
        return 1;
    }
    if (session.isRunning()) {
        loop.exec();
    }
    if (options.json) {
        writeJson(out, {{QStringLiteral("summary"), true},
                        {QStringLiteral("files"), int(files.size())},
                        {QStringLiteral("succeeded"), int(files.size()) - failed},
                        {QStringLiteral("failed"), failed}});
    }
    return failed > 0 ? 1 : 0;
}

} // namespace speecher
