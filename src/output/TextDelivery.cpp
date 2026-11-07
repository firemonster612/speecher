#include "output/TextDelivery.h"

#include "core/AppSettings.h"
#include "core/PasteKeys.h"
#ifdef SPEECHER_WITH_WAYLAND
#include "output/YdotoolDelivery.h"
#endif
#ifdef SPEECHER_WITH_MAC
#include "output/mac/MacPasteDelivery.h"
#endif
#ifdef SPEECHER_WITH_WIN
#include "output/win/WinPasteDelivery.h"
#endif

#include <QDebug>
#include <QPointer>
#include <QTimer>

#include <memory>
#include <optional>
#include <utility>

namespace speecher {

namespace {

#ifdef SPEECHER_WITH_WAYLAND
// The keyboard backends paste what TextDelivery::deliver already copied.
class YdotoolBackend final : public DeliveryBackend {
public:
    explicit YdotoolBackend(PasteMethod pasteMethod)
        : m_pasteMethod(pasteMethod)
    {
    }

    void deliver(const DeliveryContent &content,
                 const PasteGate &clearToInject,
                 std::function<void(const Outcome &)> done) override
    {
        m_ydotool.pasteFromClipboard(content.plainText, m_pasteMethod, clearToInject,
                                     [done](bool pasted, const QString &error) { done({pasted, false, error}); });
    }

private:
    PasteMethod m_pasteMethod = PasteMethod::StandardPaste;
    // Kept as long as the backend: its answer comes from ydotool's, later.
    YdotoolDelivery m_ydotool;
};

#endif // SPEECHER_WITH_WAYLAND

#if defined(SPEECHER_WITH_MAC) || defined(SPEECHER_WITH_WIN)
// The macOS and Windows target providers answer focus checks before they
// return, so their backends ask the gate as a plain question, immediately
// before the keystroke. A reply still to come counts as a refusal.
std::function<bool()> askedNow(const DeliveryBackend::PasteGate &gate)
{
    if (!gate) {
        return {};
    }
    return [gate] {
        const auto clear = std::make_shared<std::optional<bool>>();
        gate([clear](bool given) { *clear = given; });
        return clear->value_or(false);
    };
}
#endif

#ifdef SPEECHER_WITH_MAC
class MacPasteBackend final : public DeliveryBackend {
public:
    void deliver(const DeliveryContent &,
                 const PasteGate &clearToInject,
                 std::function<void(const Outcome &)> done) override
    {
        Outcome outcome;
        outcome.ok = MacPasteDelivery().paste(askedNow(clearToInject), &outcome.error);
        done(outcome);
    }
};
#endif // SPEECHER_WITH_MAC

#ifdef SPEECHER_WITH_WIN
class WinPasteBackend final : public DeliveryBackend {
public:
    explicit WinPasteBackend(PasteMethod pasteMethod)
        : m_pasteMethod(pasteMethod)
    {
    }

    void deliver(const DeliveryContent &,
                 const PasteGate &clearToInject,
                 std::function<void(const Outcome &)> done) override
    {
        Outcome outcome;
        outcome.ok = WinPasteDelivery().paste(m_pasteMethod, askedNow(clearToInject), &outcome.error);
        done(outcome);
    }

private:
    PasteMethod m_pasteMethod = PasteMethod::StandardPaste;
};
#endif // SPEECHER_WITH_WIN

} // namespace

TextDelivery::TextDelivery(QObject *parent)
    : TextDeliveryAdapter(parent)
{
    useDefaultBackendFactory();
}

TextDelivery::TextDelivery(TargetProvider *targetProvider, QObject *parent)
    : TextDeliveryAdapter(parent)
    , m_targetProvider(targetProvider)
{
    useDefaultBackendFactory();
}

TextDelivery::TextDelivery(BackendFactory backendFactory, QObject *parent)
    : TextDelivery(std::move(backendFactory), nullptr, parent)
{
}

TextDelivery::TextDelivery(BackendFactory backendFactory,
                           TargetProvider *targetProvider,
                           QObject *parent)
    : TextDeliveryAdapter(parent)
    , m_backendFactory(std::move(backendFactory))
    , m_targetProvider(targetProvider)
{
}

void TextDelivery::useDefaultBackendFactory()
{
    m_backendFactory = [](const QString &method,
                          const OutputSettings &settings,
                          PasteMethod pasteMethod) -> std::unique_ptr<DeliveryBackend> {
        Q_UNUSED(method)
        Q_UNUSED(settings)
        Q_UNUSED(pasteMethod)
#ifdef SPEECHER_WITH_WAYLAND
        if (method == QString::fromLatin1(PasteKeys::Ydotool)) {
            return std::make_unique<YdotoolBackend>(pasteMethod);
        }
#endif
#ifdef SPEECHER_WITH_MAC
        if (method == QString::fromLatin1(PasteKeys::MacPaste)) {
            return std::make_unique<MacPasteBackend>();
        }
#endif
#ifdef SPEECHER_WITH_WIN
        if (method == QString::fromLatin1(PasteKeys::WinPaste)) {
            return std::make_unique<WinPasteBackend>(pasteMethod);
        }
#endif
        return nullptr;
    };
}

// The clipboard as it was before a paste borrowed it, put back once the paste
// is verified, and what the receipt says about it. Both steps answer through
// next once the clipboard has, unless context is gone by then.
class TextDelivery::PreviousClipboard {
public:
    explicit PreviousClipboard(ClipboardDelivery &clipboard)
        : m_clipboard(clipboard)
    {
    }

    // Saves the clipboard when wanted, or notes why it could not.
    void save(bool wanted, QObject *context, std::function<void()> next)
    {
        if (!wanted) {
            next();
            return;
        }
        if (!m_clipboard.canSnapshot()) {
            m_warning = QStringLiteral("Previous clipboard could not be saved because snapshots are unavailable");
            next();
            return;
        }
        m_clipboard.capture(context, [this, next](const ClipboardCaptured &captured) {
            m_saved = captured.ok;
            m_snapshot = captured.snapshot;
            if (!m_saved) {
                m_warning = captured.error.isEmpty()
                    ? QStringLiteral("Previous clipboard could not be saved")
                    : QStringLiteral("Previous clipboard could not be saved: %1").arg(captured.error);
            }
            next();
        });
    }

    // Puts the saved clipboard back after a verified paste. next hears false
    // when nothing was saved, or when it could not be put back, which the
    // receipt then says.
    void restore(QObject *context, std::function<void(bool restored)> next)
    {
        if (!m_saved) {
            next(false);
            return;
        }
        m_clipboard.restore(m_snapshot, true, context, [this, next](const ClipboardRestored &restored) {
            if (restored.ok && restored.keptNewerCopy) {
                m_note = QStringLiteral("Previous clipboard kept as-is.");
            }
            if (!restored.ok) {
                m_warning = restored.error.isEmpty()
                    ? QStringLiteral("Previous clipboard could not be restored")
                    : QStringLiteral("Previous clipboard could not be restored: %1").arg(restored.error);
            }
            next(restored.ok);
        });
    }

    // message, followed by what became of the previous clipboard.
    QString annotate(QString message) const
    {
        if (!m_warning.isEmpty()) {
            message += QStringLiteral("; ") + m_warning;
        }
        if (!m_note.isEmpty()) {
            message += QStringLiteral(" ") + m_note;
        }
        return message;
    }

private:
    ClipboardDelivery &m_clipboard;
    ClipboardSnapshot m_snapshot;
    bool m_saved = false;
    QString m_warning;
    QString m_note;
};

// One delivery, step by step, as its plan says: copy first when Keep is on,
// then insert straight into the app, else copy (when Keep is off, saving the
// previous clipboard first) and press the paste keys, and put the previous
// clipboard back after a verified paste when Keep is off. A step that waits
// on a helper goes on from its answer. done hears the result once, and
// nothing runs after stop().
class TextDelivery::Run final : public QObject {
    // One of this run's frames on the stack. No step waits in a nested event
    // loop any more, but the run answers from inside a step, and a handler of
    // that answer may destroy the delivery, as can a provider or backend the
    // step calls into. Either stops the run; it is deleted only once no frame
    // is left that would return into it, so the backend it is in stays alive
    // too, and ~TextDelivery can delete a run with no frame at once.
    class Frame {
    public:
        explicit Frame(Run &run)
            : m_run(run)
        {
            ++m_run.m_frames;
        }
        ~Frame()
        {
            if (--m_run.m_frames == 0 && !m_run.m_done) {
                m_run.deleteLater();
            }
        }

    private:
        Run &m_run;
    };

    // step, run only while this delivery still waits for an answer: not once
    // it answered or was stopped, nor once it is gone.
    template <typename Step>
    auto whileRunning(Step step)
    {
        return [self = QPointer<Run>(this), step = std::move(step)](const auto &...answer) {
            if (self && self->m_done) {
                Frame frame(*self);
                step(answer...);
            }
        };
    }

public:
    // Takes what it needs from delivery, and no parent: delivery may go while
    // this run's frames are on the stack, which then stop() it.
    Run(TextDelivery &delivery, Request request, std::function<void(const DeliveryResult &)> done)
        : m_targetProvider(delivery.m_targetProvider)
        , m_backendFactory(delivery.m_backendFactory)
        , m_clipboard(delivery.m_clipboardDelivery)
        , m_request(std::move(request))
        , m_previousClipboard(delivery.m_clipboardDelivery)
        , m_done(std::move(done))
    {
    }

    bool running() const { return bool(m_done); }
    bool onTheStack() const { return m_frames > 0; }

    void start()
    {
        Frame frame(*this);
        m_choice = choosePlan(m_request.settings, m_request.target);
        if (!m_targetProvider) {
            focusChecked(TargetProvider::FocusMatch::Different);
            return;
        }
        m_targetProvider->focusMatch(m_request.target, this, whileRunning([this](TargetProvider::FocusMatch focus) {
            focusChecked(focus);
        }));
    }

    // Drops the steps still to come; nothing is answered after. The run
    // deletes itself once none of its frames is left on the stack.
    void stop()
    {
        m_done = {};
        if (m_frames == 0) {
            deleteLater();
        }
    }

private:
    const DeliverySteps &steps() const { return m_choice.plan.steps; }

    void focusChecked(TargetProvider::FocusMatch focus)
    {
        m_focus = focus;
        m_targetFocused = m_choice.currentFocusFallback || m_focus == TargetProvider::FocusMatch::Same;
        if (steps().keep) {
            copy([this] { insertOrPress(); });
        } else {
            insertOrPress();
        }
    }

    void finish(const DeliveryResult &result)
    {
        if (const auto done = std::exchange(m_done, {})) {
            done(result);
        }
    }

    // Puts the words on the clipboard, then goes on to next whatever the
    // copy's answer; m_copied says whether it took.
    void copy(std::function<void()> next)
    {
        m_clipboard.copy(m_request.content, this, whileRunning([this, next](const ClipboardCopied &copied) {
            m_copied = copied.ok;
            m_copyError = copied.error;
            m_copiedAsPlainText = m_request.content.html.has_value() && !copied.htmlAvailable;
            next();
        }));
    }

    void insertOrPress()
    {
        TargetProvider *provider = m_targetProvider;
        const Target &target = m_request.target;
        const QString &plainText = m_request.content.plainText;
        QString insertionError;
        if (steps().insert
            && provider
            && provider->canInsertText(target)
            && provider->insertText(target, plainText, &insertionError)) {
            provider->verifyInsertion(target, plainText, this, whileRunning([this](bool verified) {
                insertedDirectly(verified);
            }));
            return;
        }
        press(insertionError);
    }

    // The words went straight into the app; verified says they showed there.
    // An insertion that was accepted but not confirmed still ends delivery:
    // pressing the keys after it could put the words in twice.
    void insertedDirectly(bool verified)
    {
        const QString message = verified ? QStringLiteral("Pasted")
            : !steps().keep              ? QStringLiteral("Could not confirm insertion.")
            : m_copied                   ? QStringLiteral("Could not confirm insertion. Your dictation is on the clipboard.")
                       : QStringLiteral("Accepted by Target; clipboard copy failed: %1").arg(m_copyError);
        finish({
            true,
            verified ? DeliveryReceipt::VerifiedInTarget : DeliveryReceipt::AcceptedByTarget,
            m_copiedAsPlainText,
            m_previousClipboard.annotate(m_copiedAsPlainText ? message + QStringLiteral(" as plain text") : message),
        });
    }

    // Insertion was not tried or did not take: the paste keys follow, when
    // the plan presses them, focus stayed and this computer can press them.
    void press(const QString &insertionError)
    {
        if (!steps().press) {
            if (insertionError.isEmpty()) {
                finishUndelivered(copiedMessage(), QStringLiteral("no delivery step could run here"));
            } else {
                finishUndelivered(QStringLiteral("Copied; direct insertion was rejected"),
                                  QStringLiteral("direct insertion was rejected"));
            }
            return;
        }
        if (!m_targetFocused) {
            // A retitled window and a second window of the same program look
            // alike from here, and one of them is not a window change at all,
            // so that reading is kept out of the message.
            const bool titleChanged = m_focus == TargetProvider::FocusMatch::TitleChanged;
            if (!m_request.target.compositorActive) {
                finishUndelivered(copiedMessage(), QStringLiteral("the app could not be found again"));
            } else if (titleChanged) {
                finishUndelivered(QStringLiteral("The window title changed. Your dictation is on the clipboard."),
                                  QStringLiteral("the window title changed"));
            } else {
                finishUndelivered(
                    QStringLiteral("The active window changed or could not be verified. Your dictation is on the clipboard."),
                    QStringLiteral("the active window changed or could not be verified"));
            }
            return;
        }
        const QString method = pressMethod(m_request.settings);
        m_backend = method.isEmpty() || !m_backendFactory
            ? nullptr
            : m_backendFactory(method, m_request.settings,
                               m_choice.plan.terminalKeys ? PasteMethod::TerminalPaste : PasteMethod::StandardPaste);
        if (!m_backend) {
            finishUndelivered(copiedMessage(), QStringLiteral("the paste keys can't be pressed on this computer yet"));
            return;
        }
        if (steps().keep) {
            pressCopied();
            return;
        }
        // The paste borrows the clipboard: what was there goes back once the
        // paste is verified.
        m_previousClipboard.save(true, this, whileRunning([this] {
            copy([this] { pressCopied(); });
        }));
    }

    // The words are on the clipboard, unless the copy failed: the keys go
    // out once the Target is checked to still have focus.
    void pressCopied()
    {
        if (!m_copied) {
            finishCopyFailed();
            return;
        }
        // currentFocusFallback is the blind paste into whatever is in front:
        // there is no target to still be focused on.
        const bool focusGated = m_targetProvider && !m_choice.currentFocusFallback;
        if (!focusGated) {
            pressKeys(false);
            return;
        }
        // The focus gate belongs here, immediately before the keystrokes go
        // out: preparePaste marks the edit for verification, it does not
        // decide whether to paste.
        preparePasteInFocus(whileRunning([this](const QString &refusal) {
            if (!refusal.isEmpty()) {
                finishPressFailed(refusal);
                return;
            }
            pressKeys(true);
        }));
    }

    void pressKeys(bool focusGated)
    {
        // The keyboard backends wait again after the check above (modifier
        // release), so they get a last-moment gate to ask immediately before
        // the keystroke goes out. It also refuses once this run was answered,
        // blind pastes included.
        const DeliveryBackend::PasteGate clearToInject(
            [self = QPointer<Run>(this), focusGated](std::function<void(bool)> reply) {
                // Matters once a backend asks it from a later turn of the
                // event loop, as ydotool does.
                if (!self || !self->m_done) {
                    reply(false);
                    return;
                }
                if (!focusGated) {
                    reply(true);
                    return;
                }
                Frame frame(*self);
                self->m_targetProvider->focusMatch(
                    self->m_request.target, self, [self, reply](TargetProvider::FocusMatch focus) {
                        reply(focus == TargetProvider::FocusMatch::Same && self && self->m_done);
                    });
            });
        m_backend->deliver(m_request.content, clearToInject,
                           whileRunning([this](const DeliveryBackend::Outcome &outcome) {
                               if (outcome.ok) {
                                   pasted();
                               } else {
                                   finishPressFailed(outcome.error);
                               }
                           }));
    }

    // Checks that the Target still has focus and readies it for the paste.
    // next hears why the paste keys must not go out, or nothing when they may.
    void preparePasteInFocus(std::function<void(const QString &refusal)> next)
    {
        const auto checked = [this, next](TargetProvider::FocusMatch atPasteTime) {
            const Target &target = m_request.target;
            if (atPasteTime != TargetProvider::FocusMatch::Same) {
                next(!target.compositorActive
                         ? QStringLiteral("The target is no longer available for paste")
                         : atPasteTime == TargetProvider::FocusMatch::TitleChanged
                             ? QStringLiteral("The window title changed")
                             : QStringLiteral("The active window changed or could not be verified"));
                return;
            }
            next(m_targetProvider->preparePaste(target) ? QString()
                                                         : QStringLiteral("The target is no longer available for paste"));
        };
        m_targetProvider->focusMatch(m_request.target, this, whileRunning(checked));
    }

    void pasted()
    {
        if (!m_targetProvider) {
            answerPaste(false);
            return;
        }
        m_targetProvider->verifyInsertion(m_request.target, m_request.content.plainText, this,
                                          whileRunning([this](bool verified) { answerPaste(verified); }));
    }

    // The paste went out; the answer says what showed of it, after putting
    // the previous clipboard back for a verified paste when Keep is off.
    void answerPaste(bool verified)
    {
        const auto answer = [this, verified](bool restoredClipboard) {
            // A compositor-fallback target has no accessible node, so there is
            // nothing to read back: the paste can never be confirmed. Saying
            // "could not confirm" on every such dictation reads like a failure
            // when nothing went wrong.
            const bool verifiable = !m_request.target.compositorActive;
            const QString message = !verified
                ? verifiable ? QStringLiteral("Could not confirm the paste. Your dictation is on the clipboard.")
                             : QStringLiteral("Sent. Your dictation is also on the clipboard.")
                : restoredClipboard ? QStringLiteral("Sent to the app")
                                    : QStringLiteral("Copied • Sent to the app");
            finish({true, verified ? DeliveryReceipt::VerifiedInTarget : DeliveryReceipt::InputSent,
                    m_copiedAsPlainText, m_previousClipboard.annotate(message)});
        };
        if (verified && !steps().keep) {
            m_previousClipboard.restore(this, whileRunning(answer));
        } else {
            answer(false);
        }
    }

    QString copiedMessage() const
    {
        return m_copiedAsPlainText ? QStringLiteral("Copied as plain text") : QStringLiteral("Copied");
    }

    // Nothing reached the app. With Keep on the words wait on the clipboard,
    // which copiedMessage says; with it off nothing was delivered, and reason
    // says why.
    void finishUndelivered(const QString &copiedMessage, const QString &reason)
    {
        if (!steps().keep) {
            finish({false, DeliveryReceipt::None, false,
                    m_previousClipboard.annotate(QStringLiteral("Not delivered: %1").arg(reason))});
            return;
        }
        if (!m_copied) {
            finishCopyFailed();
            return;
        }
        finish({true, DeliveryReceipt::Copied, m_copiedAsPlainText, m_previousClipboard.annotate(copiedMessage)});
    }

    void finishCopyFailed()
    {
        finish({false, DeliveryReceipt::None, false,
                m_previousClipboard.annotate(m_copyError.isEmpty()
                    ? QStringLiteral("Could not copy or insert the transcription")
                    : QStringLiteral("Could not copy or insert the transcription: %1").arg(m_copyError))});
    }

    // The keys did not go out, or failed; the words are on the clipboard
    // from the copy made for them.
    void finishPressFailed(const QString &error)
    {
        finish({
            true,
            DeliveryReceipt::Copied,
            m_copiedAsPlainText,
            m_previousClipboard.annotate(error.isEmpty() ? copiedMessage()
                                                         : QStringLiteral("Copied; insertion failed: %1").arg(error)),
        });
    }

    TargetProvider *const m_targetProvider;
    const BackendFactory m_backendFactory;
    ClipboardDelivery &m_clipboard;
    const Request m_request;
    PreviousClipboard m_previousClipboard;
    std::function<void(const DeliveryResult &)> m_done;
    PlanChoice m_choice;
    TargetProvider::FocusMatch m_focus = TargetProvider::FocusMatch::Different;
    bool m_targetFocused = false;
    bool m_copied = false;
    bool m_copiedAsPlainText = false;
    QString m_copyError;
    // Kept for the whole run: it may still be on the stack when its answer
    // moves the run on.
    std::unique_ptr<DeliveryBackend> m_backend;
    int m_frames = 0;
};

TextDelivery::PlanChoice TextDelivery::choosePlan(const OutputSettings &settings, const Target &target)
{
    const PasteRule pasteRule = resolvePasteRule(settings.pasteRules, target);
    PlanChoice choice{deliveryPlan(settings.steps, pasteRule.method)};
    const bool trackableTarget = target.hasIdentity() || target.accessible;
    // Pasting blindly into whatever has focus is what the steps alone ask
    // for; an app or category rule needs its app known.
    choice.currentFocusFallback = !trackableTarget && pasteRule.scope == PasteRuleScope::Global
        && choice.plan.steps.press;
    if (target.secure || (!trackableTarget && !choice.currentFocusFallback)) {
        choice.plan.steps.insert = false;
        choice.plan.steps.press = false;
    }
    return choice;
}

TextDelivery::~TextDelivery()
{
    if (!m_run) {
        return;
    }
    m_run->stop();
    // A run with frames on the stack finishes unwinding without this. One
    // without goes now: at shutdown no event loop may be left to delete it.
    if (!m_run->onTheStack()) {
        delete m_run.data();
    }
}

void TextDelivery::setAnswerDeadlineMs(int ms)
{
    m_answerDeadlineMs = ms;
}

void TextDelivery::deliver(const OutputSettings &settings,
                           const DeliveryContent &content,
                           const Target &target)
{
    m_requests.append({settings, content, target});
    // Never inside this call: with a provider and backends that answer at
    // once, the run would otherwise answer before deliver() returns.
    QMetaObject::invokeMethod(this, &TextDelivery::startNext, Qt::QueuedConnection);
}

void TextDelivery::startNext()
{
    if (m_run || m_requests.isEmpty()) {
        return;
    }
    Run *run = new Run(*this, m_requests.first(), [this](const DeliveryResult &result) { answer(result); });
    m_run = run;
    // Only once the run is gone: a stopped one can still have frames on the
    // stack, and the next must not start inside them.
    connect(run, &QObject::destroyed, this, &TextDelivery::startNext, Qt::QueuedConnection);
    QTimer::singleShot(m_answerDeadlineMs, run, [this, run] {
        if (!run->running()) {
            return;
        }
        qWarning() << "text delivery did not finish within" << m_answerDeadlineMs << "ms";
        answer({false, DeliveryReceipt::None, false, QStringLiteral("Delivery did not finish in time")});
    });
    run->start();
}

void TextDelivery::answer(const DeliveryResult &result)
{
    m_run->stop();
    m_requests.removeFirst();
    emit delivered(result);
}

QString TextDelivery::pressMethod(const OutputSettings &settings)
{
#if defined(SPEECHER_WITH_MAC)
    Q_UNUSED(settings)
    return QString::fromLatin1(PasteKeys::MacPaste);
#elif defined(SPEECHER_WITH_WIN)
    Q_UNUSED(settings)
    return QString::fromLatin1(PasteKeys::WinPaste);
#else
    return settings.ydotoolEnabled ? QString::fromLatin1(PasteKeys::Ydotool) : QString();
#endif
}

} // namespace speecher
