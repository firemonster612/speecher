#pragma once

#include "core/AppSettings.h"
#include "core/OutputFormat.h"
#include "core/ProviderFailure.h"
#include "core/Target.h"
#include "dictation/PopupPresentation.h"

#include <functional>
#include <optional>

#include <QList>
#include <QObject>
#include <QStringList>

namespace speecher {

struct DeliveryContent {
    QString plainText;
    std::optional<QString> html;
};

DeliveryContent makeDeliveryContent(const QString &text, OutputFormat format);

enum class DeliveryReceipt {
    None,
    Copied,
    InputSent,
    AcceptedByTarget,
    VerifiedInTarget,
};

struct DeliveryResult {
    bool ok = false;
    DeliveryReceipt receipt = DeliveryReceipt::None;
    bool formatDowngraded = false;
    QString message;
};

// Readies a provider off the GUI thread: run() on a worker, then apply() back
// on the GUI thread with what run() returned. A preparation reports the
// failure that stopped it, or nothing once the provider is ready.
struct PreparationJob {
    bool showRefreshIndicator = false;
    std::function<std::optional<ProviderFailure>()> run;
    std::function<void(const std::optional<ProviderFailure> &)> apply;
};

struct AudioInputDeviceInfo {
    QString id;
    QString label;
    bool isDefault = false;
};

struct SpeechFailure {
    quint64 attemptId = 0;
    QString message;
    bool retryable = false;
    SpeechPhase phase = SpeechPhase::Connect;
    ProviderFailureKind kind = ProviderFailureKind::Other;
    int httpStatus = 0;
};

class AudioInput : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;
    virtual void warmUp() {}
    // Records from this device rather than the one the settings name, for an
    // input started once and then thrown away. Inputs that read no device,
    // such as a file standing in for the microphone, ignore it.
    virtual void useDevice(const QString &deviceId) { Q_UNUSED(deviceId) }
    // start() may wait for the first audio and stop() for the post-roll in a
    // nested event loop. Shortcut presses, IPC, settings changes and failed()
    // can all arrive meanwhile, so a caller may be re-entered, including by
    // another start() or stop(), before either returns.
    virtual bool start(QString *error = nullptr) = 0;
    virtual void stop() = 0;
    virtual bool isActive() const = 0;
    // Forgets the audio a warm input keeps rolling for the next start, so a
    // resume after a pause does not send what was said during it.
    virtual void clearPreRoll() {}
    // Delivers quiet audio too from now on, whatever Skip silence says, for
    // an echo canceller, which needs the audio whole. Inputs that never skip
    // silence ignore it.
    virtual void keepSilence() {}
    // Why a started input that asks the system to cancel echo records
    // without it, for `record start` and `record status`; nothing for one
    // that has it or never asks.
    virtual QString echoCancellationWarning() const { return {}; }

signals:
    void audioChunk(const QByteArray &pcm);
    void levelChanged(float level);
    void failed(const QString &message);
};

class MediaController : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;
    virtual void pausePlaying() = 0;
    virtual void resumePaused() = 0;
};

class TargetProvider : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;
    // What the platform sees of the focused target. Its category is core's
    // to decide: see classifyTarget.
    virtual Target capture() = 0;
    virtual bool stillFocused(const Target &target)
    {
        Q_UNUSED(target);
        return false;
    }
    // How the window in front now relates to the one dictation started in.
    // TitleChanged is its own answer because it is the ambiguous one: the same
    // window after a retitle, or a second window of the same program. Either
    // way it is not safe to paste into, and neither is a lost window, so
    // telling the person their window "changed" would be a guess.
    enum class FocusMatch {
        Same,
        TitleChanged,
        Different,
    };
    // focusMatch() and verifyInsertion() may wait in a nested event loop
    // (asking the compositor which window is in front, or giving the app time
    // to show the inserted text). Timers, IPC and D-Bus replies run
    // meanwhile, so a caller may be re-entered before either returns.
    virtual FocusMatch focusMatch(const Target &target)
    {
        return stillFocused(target) ? FocusMatch::Same : FocusMatch::Different;
    }
    virtual bool canInsertText(const Target &target)
    {
        Q_UNUSED(target);
        return false;
    }
    virtual bool insertText(const Target &target, const QString &plainText, QString *error = nullptr)
    {
        Q_UNUSED(target);
        Q_UNUSED(plainText);
        if (error) {
            *error = QStringLiteral("Direct text insertion is unavailable");
        }
        return false;
    }
    // Called immediately before synthetic paste so verification can distinguish
    // that edit from changes made while dictation was in progress.
    virtual bool preparePaste(const Target &target)
    {
        Q_UNUSED(target);
        return true;
    }
    virtual bool verifyInsertion(const Target &target, const QString &plainText)
    {
        Q_UNUSED(target);
        Q_UNUSED(plainText);
        return false;
    }
    virtual void setCorrectionObservationEnabled(bool enabled)
    {
        Q_UNUSED(enabled);
    }

signals:
    void correctionObserved(const QString &original,
                            const QString &corrected,
                            const QString &applicationId,
                            double confidence);
};

class ScreenshotContextProvider : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;
    virtual void capture() = 0;
    virtual void cancel() = 0;

signals:
    void captured(const QByteArray &data, const QString &mediaType);
    void failed(const QString &message);
};

// A stretch of the Raw Transcript and when it was spoken, in milliseconds from
// the start of the attempt's audio.
struct TranscriptSegment {
    qint64 startMs = 0;
    qint64 endMs = 0;
    QString text;

    bool operator==(const TranscriptSegment &) const = default;
};

class SpeechTranscriber : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;
    // The sign-in methods take the accounts a provider that signs in reads
    // its credentials from.
    virtual bool requiresRefresh(const SpeechSettings &settings, const AccountSettings &accounts) const = 0;
    virtual std::optional<PreparationJob> createPrepareJob(const SpeechSettings &settings,
                                                           const AccountSettings &accounts)
    {
        Q_UNUSED(settings);
        Q_UNUSED(accounts);
        return std::nullopt;
    }
    // A failure sends a Dictation Session on to the next provider whatever its
    // kind, so one the provider cannot classify is Unavailable.
    virtual std::optional<ProviderFailure> prepare(const SpeechSettings &settings,
                                                   const AccountSettings &accounts) = 0;
    // Whether prepare() renews a sign-in that expires, so one the service
    // turned down may be taken once it has run again. A key from Settings
    // is not renewed.
    virtual bool renewsSignIn() const { return false; }
    // How long the provider may take to answer an utterance once it has all
    // of it; a recording's stop waits that long for each one left.
    virtual int utteranceAnswerTimeoutMs() const { return 15000; }
    // Whether finalTranscript arrives while the audio still streams, an
    // utterance at a time, by itself or at each endUtterance(), rather than
    // all at the end. A recording needs it.
    virtual bool streamsFinalText(const SpeechSettings &settings) const
    {
        Q_UNUSED(settings);
        return false;
    }
    virtual void startAttempt(quint64 attemptId, const SpeechSettings &settings) = 0;
    virtual void sendAudio(quint64 attemptId, const QByteArray &pcm) = 0;
    virtual void finishInput(quint64 attemptId) = 0;
    virtual void cancelAttempt(quint64 attemptId) = 0;
    // Finalizes the utterance heard so far while the stream carries on, for a
    // provider that does not end one at a silence by itself. Providers that do
    // ignore it.
    virtual void endUtterance(quint64 attemptId) { Q_UNUSED(attemptId) }
    // With SpeechSettings::cutIntoUtterances, the audio from here on is the
    // next utterance, and what came since the last ended was quiet. Providers
    // that take the quiet too ignore it.
    virtual void beginUtterance(quint64 attemptId) { Q_UNUSED(attemptId) }

signals:
    // Optional: the service accepted the attempt's stream, so audio sent from
    // here on reaches it. Text from the attempt implies it.
    void attemptConnected(quint64 attemptId);
    void partialTranscript(quint64 attemptId, const QString &text);
    void finalTranscript(quint64 attemptId, const QString &text);
    // Optional: a whole-attempt transcript that supersedes the accumulated
    // finals, emitted before attemptCompleted by providers that retranscribe
    // the buffered audio in one pass at the end.
    void attemptTranscript(quint64 attemptId, const QString &text);
    // Optional: when each part of that whole-attempt transcript was spoken,
    // emitted just before it by providers whose service returns timings.
    void attemptSegments(quint64 attemptId, const QList<speecher::TranscriptSegment> &segments);
    void attemptCompleted(quint64 attemptId);
    void failed(const speecher::SpeechFailure &failure);
    // Optional: an utterance endUtterance() ended was not transcribed, or
    // only in part, and the attempt goes on with the next.
    void utteranceFailed(const speecher::SpeechFailure &failure);
};

class TranscriptRefiner : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;
    // As SpeechTranscriber's, these and refine() take the accounts a provider
    // that signs in reads its credentials from. A sign-in that expires renews
    // in the refresh job, whose showRefreshIndicator says it is due.
    virtual std::optional<PreparationJob> createRefreshJob(const RefinementSettings &settings,
                                                           const AccountSettings &accounts)
    {
        Q_UNUSED(settings);
        Q_UNUSED(accounts);
        return std::nullopt;
    }
    virtual std::optional<ProviderFailure> prepare(const RefinementSettings &settings,
                                                   const AccountSettings &accounts) = 0;
    virtual void refine(const QString &rawTranscript,
                        const QStringList &vocabulary,
                        const RefinementContext &context,
                        const RefinementSettings &settings,
                        const AccountSettings &accounts) = 0;
    virtual void cancel() = 0;

signals:
    void delta(const QString &text);
    void completed(const QString &text);
    void failed(const speecher::ProviderFailure &failure);
};

class TextDeliveryAdapter : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;
    virtual DeliveryResult deliver(const OutputSettings &settings,
                                   const DeliveryContent &content,
                                   const Target &target) = 0;
};

} // namespace speecher
