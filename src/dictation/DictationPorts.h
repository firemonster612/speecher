#pragma once

#include "core/AppSettings.h"
#include "core/OutputFormat.h"
#include "core/Target.h"
#include "dictation/PopupPresentation.h"

#include <functional>
#include <optional>

#include <QList>
#include <QObject>
#include <QStringList>
#include <QTimer>

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

struct AudioInputDeviceInfo {
    QString id;
    QString label;
    bool isDefault = false;
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
    // start(), stop() and abort() return at once, and no signal is emitted
    // from inside them, so a caller is never re-entered by its own call. A
    // signal's handler may call them, and the input then says nothing more
    // of the capture that handler ended.
    //
    // Asks for capture: started() follows once audio flows, or failed() when
    // the input cannot capture. Audio may arrive before either. A start()
    // while capturing first ends that capture, with no signal for it.
    virtual void start() = 0;
    // Asks capture to end: the post-roll still arrives as audioChunk, then
    // stopped(), even when capture fails meanwhile. A stop() while starting
    // drops the start: stopped() follows and started() never does. Nothing
    // while off or already stopping.
    virtual void stop() = 0;
    // Ends capture now, with no post-roll and no signal at all for it, a start
    // or stop under way included.
    virtual void abort() = 0;
    // True from start() until stopped(), failed() or abort(), opening and
    // post-roll included. An input with no post-roll may turn false at stop(),
    // before its stopped() arrives.
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
    void started();
    void stopped();
    // The input could not start, or capture ended without a stop(); nothing
    // follows.
    void failed(const QString &message);

protected:
    // Runs report on the next turn of the event loop, unless dropReports()
    // comes first: how an input says started(), stopped() or failed() without
    // saying it inside the call that led to it.
    void reportLater(std::function<void()> report)
    {
        QTimer::singleShot(0, this, [this, reports = m_reports, report = std::move(report)] {
            if (reports == m_reports) {
                report();
            }
        });
    }
    // For start() and abort(): what an earlier capture had yet to report is
    // never reported.
    void dropReports() { ++m_reports; }

private:
    quint64 m_reports = 0;
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
    // to decide: see classifyTarget. done runs once, unless context is gone
    // by then: before this returns where the platform knows at once, later
    // where it must ask the compositor.
    virtual void capture(QObject *context, std::function<void(const Target &)> done) = 0;
    // Whether the target still has focus, for a provider that knows at once;
    // focusMatch() answers from it unless the provider asks otherwise.
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
    // Answers through done as capture() does.
    virtual void focusMatch(const Target &target, QObject *context, std::function<void(FocusMatch)> done)
    {
        Q_UNUSED(context);
        done(stillFocused(target) ? FocusMatch::Same : FocusMatch::Different);
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
    // May wait in a nested event loop, giving the app time to show the
    // inserted text. Timers, IPC and D-Bus replies run meanwhile, so a caller
    // may be re-entered before it returns.
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

class TextDeliveryAdapter : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;
    // Returns at once; delivered() follows with what became of the words,
    // never from inside deliver(). Deliveries are answered in the order they
    // were asked for.
    virtual void deliver(const OutputSettings &settings,
                         const DeliveryContent &content,
                         const Target &target) = 0;

signals:
    void delivered(const speecher::DeliveryResult &result);
};

} // namespace speecher
