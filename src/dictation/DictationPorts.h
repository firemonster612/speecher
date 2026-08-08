#pragma once

#include "core/AppSettings.h"
#include "core/OutputFormat.h"
#include "core/Target.h"

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

struct SpeechPrepareResult {
    bool ok = false;
    QString message;
};

struct RefinementPrepareResult {
    bool ok = false;
    QString message;
};

struct SpeechPrepareJob {
    bool showRefreshIndicator = false;
    std::function<SpeechPrepareResult()> run;
    std::function<void(const SpeechPrepareResult &)> apply;
};

struct RefinementRefreshResult {
    bool ok = true;
    QString message;
};

struct RefinementRefreshJob {
    bool showRefreshIndicator = false;
    std::function<RefinementRefreshResult()> run;
    std::function<void(const RefinementRefreshResult &)> apply;
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
    QString phase;
};

class AudioInput : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;
    virtual bool start(QString *error = nullptr) = 0;
    virtual void stop() = 0;
    virtual bool isActive() const = 0;

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
    virtual Target capture() = 0;
    virtual bool stillFocused(const Target &target)
    {
        Q_UNUSED(target);
        return false;
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

class SpeechTranscriber : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;
    virtual QString id() const = 0;
    virtual QString label() const = 0;
    virtual bool requiresRefresh(const SpeechSettings &settings) const = 0;
    virtual std::optional<SpeechPrepareJob> createPrepareJob(const SpeechSettings &settings)
    {
        Q_UNUSED(settings);
        return std::nullopt;
    }
    virtual SpeechPrepareResult prepare(const SpeechSettings &settings) = 0;
    virtual void startAttempt(quint64 attemptId, const SpeechSettings &settings) = 0;
    virtual void sendAudio(quint64 attemptId, const QByteArray &pcm) = 0;
    virtual void finishInput(quint64 attemptId) = 0;
    virtual void cancelAttempt(quint64 attemptId) = 0;

signals:
    void partialTranscript(quint64 attemptId, const QString &text);
    void finalTranscript(quint64 attemptId, const QString &text);
    void attemptCompleted(quint64 attemptId);
    void failed(const speecher::SpeechFailure &failure);
};

class TranscriptRefiner : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;
    virtual QString id() const = 0;
    virtual QString label() const = 0;
    virtual bool requiresRefresh(const RefinementSettings &settings) const = 0;
    virtual std::optional<RefinementRefreshJob> createRefreshJob(const RefinementSettings &settings)
    {
        Q_UNUSED(settings);
        return std::nullopt;
    }
    virtual void refresh(const RefinementSettings &settings) = 0;
    virtual RefinementPrepareResult prepare(const RefinementSettings &settings) = 0;
    virtual bool supportsScreenshotContext(const RefinementSettings &settings) const
    {
        Q_UNUSED(settings);
        return false;
    }
    virtual void refine(const QString &rawTranscript,
                        const QStringList &vocabulary,
                        const RefinementContext &context,
                        const RefinementSettings &settings) = 0;
    virtual void cancel() = 0;

signals:
    void delta(const QString &text);
    void completed(const QString &text);
    void failed(const QString &message);
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
