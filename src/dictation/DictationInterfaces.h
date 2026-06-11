#pragma once

#include "core/AppSettings.h"
#include "output/DeliveryResult.h"

#include <functional>
#include <QObject>
#include <QList>
#include <QStringList>
#include <optional>

namespace speecher {

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
    virtual void start(const SpeechSettings &settings) = 0;
    virtual void sendAudio(const QByteArray &pcm) = 0;
    virtual void stop() = 0;

signals:
    void partialTranscript(const QString &text);
    void finalTranscript(const QString &text);
    void failed(const QString &message);
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
    virtual void refine(const QString &rawTranscript,
                        const QStringList &vocabulary,
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
    virtual DeliveryResult deliver(const OutputSettings &settings, const QString &text) = 0;
};

} // namespace speecher
