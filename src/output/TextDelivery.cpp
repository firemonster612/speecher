#include "output/TextDelivery.h"

#include "core/AppSettings.h"
#include "core/OutputMethod.h"
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
#include <utility>
#include <vector>

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
                 const std::function<bool()> &clearToInject,
                 std::function<void(const Outcome &)> done) override
    {
        Outcome outcome;
        outcome.ok = YdotoolDelivery().pasteFromClipboard(content.plainText, m_pasteMethod, clearToInject, &outcome.error);
        done(outcome);
    }

private:
    PasteMethod m_pasteMethod = PasteMethod::StandardPaste;
};

class WlCopyBackend final : public DeliveryBackend {
public:
    explicit WlCopyBackend(ClipboardDelivery *clipboardDelivery)
        : m_clipboardDelivery(clipboardDelivery)
    {
    }

    void deliver(const DeliveryContent &content,
                 const std::function<bool()> &,
                 std::function<void(const Outcome &)> done) override
    {
        m_clipboardDelivery->copyWayland(content, m_clipboardDelivery, std::move(done));
    }

private:
    ClipboardDelivery *m_clipboardDelivery = nullptr;
};
#endif // SPEECHER_WITH_WAYLAND

#ifdef SPEECHER_WITH_MAC
class MacPasteBackend final : public DeliveryBackend {
public:
    void deliver(const DeliveryContent &,
                 const std::function<bool()> &clearToInject,
                 std::function<void(const Outcome &)> done) override
    {
        Outcome outcome;
        outcome.ok = MacPasteDelivery().paste(clearToInject, &outcome.error);
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
                 const std::function<bool()> &clearToInject,
                 std::function<void(const Outcome &)> done) override
    {
        Outcome outcome;
        outcome.ok = WinPasteDelivery().paste(m_pasteMethod, clearToInject, &outcome.error);
        done(outcome);
    }

private:
    PasteMethod m_pasteMethod = PasteMethod::StandardPaste;
};
#endif // SPEECHER_WITH_WIN

class QtClipboardBackend final : public DeliveryBackend {
public:
    explicit QtClipboardBackend(ClipboardDelivery *clipboardDelivery)
        : m_clipboardDelivery(clipboardDelivery)
    {
    }

    void deliver(const DeliveryContent &content,
                 const std::function<bool()> &,
                 std::function<void(const Outcome &)> done) override
    {
        m_clipboardDelivery->copyQt(content, m_clipboardDelivery, std::move(done));
    }

private:
    ClipboardDelivery *m_clipboardDelivery = nullptr;
};

// Whether delivery tries inserting straight into the app first, and whether
// that is all it tries before leaving the words on the clipboard.
struct DirectInsertion {
    bool requested = false;
    bool only = false;
};

DirectInsertion directInsertion(const QString &outputMethod, PasteMethod pasteMethod)
{
    if (pasteMethod == PasteMethod::ClipboardOnly) {
        return {};
    }
    const bool asked = pasteMethod == PasteMethod::DirectInsert
        || outputMethod == QString::fromLatin1(OutputMethod::DirectInsert);
    return {asked || outputMethod == QString::fromLatin1(OutputMethod::Automatic), asked};
}

// The methods that press the paste keys in the target, rather than copy.
bool pressesPasteKeys(const QString &method)
{
    return method == QString::fromLatin1(OutputMethod::Ydotool)
        || method == QString::fromLatin1(OutputMethod::MacPaste)
        || method == QString::fromLatin1(OutputMethod::WinPaste);
}

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
    m_backendFactory = [this](const QString &method,
                              const OutputSettings &settings,
                              PasteMethod pasteMethod) -> std::unique_ptr<DeliveryBackend> {
        Q_UNUSED(settings)
        Q_UNUSED(pasteMethod)
#ifdef SPEECHER_WITH_WAYLAND
        if (method == QString::fromLatin1(OutputMethod::Ydotool)) {
            return std::make_unique<YdotoolBackend>(pasteMethod);
        }
        if (method == QString::fromLatin1(OutputMethod::WlCopy)) {
            return std::make_unique<WlCopyBackend>(&m_clipboardDelivery);
        }
#endif
#ifdef SPEECHER_WITH_MAC
        if (method == QString::fromLatin1(OutputMethod::MacPaste)) {
            return std::make_unique<MacPasteBackend>();
        }
#endif
#ifdef SPEECHER_WITH_WIN
        if (method == QString::fromLatin1(OutputMethod::WinPaste)) {
            return std::make_unique<WinPasteBackend>(pasteMethod);
        }
#endif
        if (method == QString::fromLatin1(OutputMethod::QtClipboard)) {
            return std::make_unique<QtClipboardBackend>(&m_clipboardDelivery);
        }
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

// One delivery, step by step: save the previous clipboard, copy, then insert
// straight into the app or press the paste keys, and put the previous
// clipboard back after a verified paste. A step that waits on a helper goes
// on from its answer. done hears the result once, and nothing runs after
// stop().
class TextDelivery::Run final : public QObject {
    // One of this run's frames on the stack. The deadline can stop the run
    // from a step's nested event loop, such as the KWin probe's; the run is
    // deleted only once no frame is left that would return into it, so the
    // backend it is in stays alive too.
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
    Run(TextDelivery &delivery, Request request, std::function<void(const DeliveryResult &)> done)
        : QObject(&delivery)
        , m_delivery(delivery)
        , m_request(std::move(request))
        , m_previousClipboard(delivery.m_clipboardDelivery)
        , m_done(std::move(done))
    {
    }

    void start()
    {
        Frame frame(*this);
        const OutputSettings &settings = m_request.settings;
        m_choice = choosePasteMethod(settings, m_request.target);
        m_pasteMethod = m_choice.method;
        m_insertion = directInsertion(OutputMethod::normalized(settings.method), m_pasteMethod);
        m_focus = m_delivery.m_targetProvider ? m_delivery.m_targetProvider->focusMatch(m_request.target)
                                              : TargetProvider::FocusMatch::Different;
        // focusMatch() and verifyInsertion() can wait in a nested event loop,
        // where the deadline may stop this run: it goes no further.
        if (!m_done) {
            return;
        }
        m_targetFocused = m_pasteMethod != PasteMethod::ClipboardOnly
            && (m_choice.currentFocusFallback || m_focus == TargetProvider::FocusMatch::Same);
        m_previousClipboard.save(m_pasteMethod != PasteMethod::ClipboardOnly && settings.restoreClipboardAfterTyping,
                                 this, whileRunning([this] { copy(); }));
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
    void finish(const DeliveryResult &result)
    {
        if (const auto done = std::exchange(m_done, {})) {
            done(result);
        }
    }

    void copy()
    {
        m_delivery.m_clipboardDelivery.copy(m_request.content, this, whileRunning([this](const ClipboardCopied &copied) {
            m_copiedInitially = copied.ok;
            m_initialCopyError = copied.error;
            m_copiedAsPlainText = m_request.content.html.has_value() && !copied.htmlAvailable;
            insertOrPaste();
        }));
    }

    void insertOrPaste()
    {
        TargetProvider *provider = m_delivery.m_targetProvider;
        const Target &target = m_request.target;
        const QString &plainText = m_request.content.plainText;
        QString insertionError;
        if (m_insertion.requested
            && provider
            && provider->canInsertText(target)
            && provider->insertText(target, plainText, &insertionError)) {
            const bool verified = provider->verifyInsertion(target, plainText);
            if (!m_done) {
                return;
            }
            const auto answer = [this, verified] {
                const QString message = verified
                    ? QStringLiteral("Pasted")
                    : m_copiedInitially
                        ? QStringLiteral("Could not confirm insertion. Your dictation is on the clipboard.")
                        : QStringLiteral("Accepted by Target; clipboard copy failed: %1").arg(m_initialCopyError);
                finish({
                    true,
                    verified ? DeliveryReceipt::VerifiedInTarget : DeliveryReceipt::AcceptedByTarget,
                    m_copiedAsPlainText,
                    m_previousClipboard.annotate(
                        m_copiedAsPlainText ? message + QStringLiteral(" as plain text") : message),
                });
            };
            if (m_copiedInitially && verified) {
                m_previousClipboard.restore(this, whileRunning([answer](bool) { answer(); }));
            } else {
                answer();
            }
            return;
        }
        if (!m_copiedInitially) {
            finish({false, DeliveryReceipt::None, false,
                    m_previousClipboard.annotate(m_initialCopyError.isEmpty()
                        ? QStringLiteral("Could not copy or insert the transcription")
                        : QStringLiteral("Could not copy or insert the transcription: %1").arg(m_initialCopyError))});
            return;
        }
        if (m_insertion.only) {
            if (!insertionError.isEmpty()) {
                finish({
                    true,
                    DeliveryReceipt::Copied,
                    m_copiedAsPlainText,
                    m_previousClipboard.annotate(QStringLiteral("Copied; direct insertion was rejected")),
                });
                return;
            }
            m_pasteMethod = PasteMethod::ClipboardOnly;
        }
        QString focusWarning;
        if (m_pasteMethod != PasteMethod::ClipboardOnly && !m_targetFocused) {
            if (target.compositorActive) {
                // A retitled window and a second window of the same program look
                // alike from here, and one of them is not a window change at all,
                // so that reading is kept out of the message.
                focusWarning = m_focus == TargetProvider::FocusMatch::TitleChanged
                    ? QStringLiteral("The window title changed. Your dictation is on the clipboard.")
                    : QStringLiteral("The active window changed or could not be verified. Your dictation is on the clipboard.");
            }
            m_pasteMethod = PasteMethod::ClipboardOnly;
        }

        if (m_pasteMethod == PasteMethod::ClipboardOnly) {
            finish({
                true,
                DeliveryReceipt::Copied,
                m_copiedAsPlainText,
                m_previousClipboard.annotate(
                    !focusWarning.isEmpty() ? focusWarning
                        : m_copiedAsPlainText ? QStringLiteral("Copied as plain text") : QStringLiteral("Copied")),
            });
            return;
        }
        m_methods = orderedMethods(m_request.settings, m_choice.method);
        pasteThroughBackend(0);
    }

    // Tries the backends for the paste method from index on, in turn.
    void pasteThroughBackend(qsizetype index)
    {
        std::unique_ptr<DeliveryBackend> backend;
        for (; index < m_methods.size() && !backend; ++index) {
            backend = m_delivery.m_backendFactory
                ? m_delivery.m_backendFactory(m_methods.at(index), m_request.settings, m_choice.method)
                : nullptr;
        }
        if (!backend) {
            finishCopied();
            return;
        }
        const QString method = m_methods.at(index - 1);
        TargetProvider *provider = m_delivery.m_targetProvider;
        const bool virtualKeyboardInput = pressesPasteKeys(method);
        // currentFocusFallback is the blind paste into whatever is in front:
        // there is no target to still be focused on.
        const bool focusGated = virtualKeyboardInput && provider && !m_choice.currentFocusFallback;
        // The focus gate belongs here, immediately before the keystrokes go
        // out: preparePaste marks the edit for verification, it does not
        // decide whether to paste.
        if (focusGated) {
            const QString refusal = m_delivery.preparePasteInFocus(m_request.target);
            if (!m_done) {
                return;
            }
            if (!refusal.isEmpty()) {
                m_firstError = refusal;
                finishCopied();
                return;
            }
        }
        // The keyboard backends wait again after the check above (modifier
        // release), so they get a last-moment gate to call immediately before
        // the keystroke goes out. It also refuses once this run was answered,
        // blind pastes included.
        const std::function<bool()> clearToInject = virtualKeyboardInput
            ? std::function<bool()>([self = QPointer<Run>(this), focusGated] {
                  if (!self || !self->m_done) {
                      return false;
                  }
                  if (!focusGated) {
                      return true;
                  }
                  Frame frame(*self);
                  const bool focused = self->m_delivery.m_targetProvider->focusMatch(self->m_request.target)
                      == TargetProvider::FocusMatch::Same;
                  return focused && self->m_done;
              })
            : std::function<bool()>();
        // Kept for the whole run: a backend may still be on the stack when
        // its answer moves on to the next.
        m_backends.push_back(std::move(backend));
        m_backends.back()->deliver(
            m_request.content, clearToInject,
            whileRunning([this, index, method, virtualKeyboardInput](const DeliveryBackend::Outcome &outcome) {
                if (outcome.ok) {
                    pasted(method, virtualKeyboardInput, outcome.htmlAvailable);
                    return;
                }
                if (m_firstError.isEmpty()) {
                    m_firstError = outcome.error;
                }
                // A failed paste already left the text on the clipboard, so the
                // later clipboard methods would only re-report what the caller
                // already has.
                if (virtualKeyboardInput) {
                    finishCopied();
                    return;
                }
                pasteThroughBackend(index);
            }));
    }

    void pasted(const QString &method, bool virtualKeyboardInput, bool htmlAvailable)
    {
        TargetProvider *provider = m_delivery.m_targetProvider;
        const bool copied = method == QString::fromLatin1(OutputMethod::WlCopy)
            || method == QString::fromLatin1(OutputMethod::QtClipboard);
        // A keyboard backend pastes the copy deliver() made.
        const bool downgraded = virtualKeyboardInput ? m_copiedAsPlainText
                                                     : m_request.content.html.has_value() && !htmlAvailable;
        const bool verified = !copied
            && provider
            && provider->verifyInsertion(m_request.target, m_request.content.plainText);
        if (!m_done) {
            return;
        }
        const auto answer = [this, copied, downgraded, verified, virtualKeyboardInput](bool restoredClipboard) {
            // A compositor-fallback target has no accessible node, so there is
            // nothing to read back: the paste can never be confirmed. Saying
            // "could not confirm" on every such dictation reads like a failure
            // when nothing went wrong.
            const bool verifiable = !m_request.target.compositorActive;
            const QString message = copied
                ? QStringLiteral("Copied")
                : !verified
                    ? verifiable
                        ? QStringLiteral("Could not confirm the paste. Your dictation is on the clipboard.")
                        : QStringLiteral("Sent. Your dictation is also on the clipboard.")
                    : virtualKeyboardInput
                    ? restoredClipboard
                        ? QStringLiteral("Sent to the app")
                        : QStringLiteral("Copied • Sent to the app")
                    : verified ? QStringLiteral("Pasted") : QStringLiteral("Sent to the app");
            finish({true,
                    copied ? DeliveryReceipt::Copied
                           : verified ? DeliveryReceipt::VerifiedInTarget : DeliveryReceipt::InputSent,
                    downgraded,
                    m_previousClipboard.annotate(
                        downgraded && !virtualKeyboardInput ? message + QStringLiteral(" as plain text") : message)});
        };
        if (virtualKeyboardInput && verified) {
            m_previousClipboard.restore(this, whileRunning(answer));
        } else {
            answer(false);
        }
    }

    // No backend delivered; the words are on the clipboard from the copy.
    void finishCopied()
    {
        finish({
            true,
            DeliveryReceipt::Copied,
            m_copiedAsPlainText,
            m_previousClipboard.annotate(
                m_firstError.isEmpty()
                    ? (m_copiedAsPlainText ? QStringLiteral("Copied as plain text") : QStringLiteral("Copied"))
                    : QStringLiteral("Copied; insertion failed: %1").arg(m_firstError)),
        });
    }

    TextDelivery &m_delivery;
    const Request m_request;
    PreviousClipboard m_previousClipboard;
    std::function<void(const DeliveryResult &)> m_done;
    PasteChoice m_choice;
    PasteMethod m_pasteMethod = PasteMethod::ClipboardOnly;
    DirectInsertion m_insertion;
    TargetProvider::FocusMatch m_focus = TargetProvider::FocusMatch::Different;
    bool m_targetFocused = false;
    bool m_copiedInitially = false;
    bool m_copiedAsPlainText = false;
    QString m_initialCopyError;
    QStringList m_methods;
    QString m_firstError;
    std::vector<std::unique_ptr<DeliveryBackend>> m_backends;
    int m_frames = 0;
};

TextDelivery::PasteChoice TextDelivery::choosePasteMethod(const OutputSettings &settings, const Target &target)
{
    const PasteRule pasteRule = resolvePasteRule(settings.pasteRules, target);
    const bool trackableTarget = target.hasIdentity() || target.accessible;
    const bool currentFocusFallback = !trackableTarget
        && pasteRule.scope == PasteRuleScope::Global
        && (pasteRule.method == PasteMethod::StandardPaste
            || pasteRule.method == PasteMethod::TerminalPaste);
    const bool pastes = !target.secure && (trackableTarget || currentFocusFallback);
    return {pastes ? pasteRule.method : PasteMethod::ClipboardOnly, currentFocusFallback};
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
    // Never inside this call: a step's nested event loop, to check focus or
    // verify an insertion, could otherwise answer an earlier delivery here.
    QMetaObject::invokeMethod(this, &TextDelivery::startNext, Qt::QueuedConnection);
}

void TextDelivery::startNext()
{
    if (m_run || m_requests.isEmpty()) {
        return;
    }
    Run *run = new Run(*this, m_requests.first(), [this](const DeliveryResult &result) { answer(result); });
    m_run = run;
    QTimer::singleShot(m_answerDeadlineMs, run, [this, run] {
        if (m_run != run) {
            return;
        }
        qWarning() << "text delivery did not finish within" << m_answerDeadlineMs << "ms";
        answer({false, DeliveryReceipt::None, false, QStringLiteral("Delivery did not finish in time")});
    });
    run->start();
}

void TextDelivery::answer(const DeliveryResult &result)
{
    std::exchange(m_run, nullptr)->stop();
    m_requests.removeFirst();
    QMetaObject::invokeMethod(this, &TextDelivery::startNext, Qt::QueuedConnection);
    emit delivered(result);
}

QString TextDelivery::preparePasteInFocus(const Target &target)
{
    const TargetProvider::FocusMatch atPasteTime = m_targetProvider->focusMatch(target);
    if (atPasteTime != TargetProvider::FocusMatch::Same) {
        return !target.compositorActive
            ? QStringLiteral("The target is no longer available for paste")
            : atPasteTime == TargetProvider::FocusMatch::TitleChanged
                ? QStringLiteral("The window title changed")
                : QStringLiteral("The active window changed or could not be verified");
    }
    if (!m_targetProvider->preparePaste(target)) {
        return QStringLiteral("The target is no longer available for paste");
    }
    return {};
}

QStringList TextDelivery::pasteRoute(const OutputSettings &settings, PasteMethod pasteMethod)
{
    const QString copy = QStringLiteral("copy");
    if (pasteMethod == PasteMethod::ClipboardOnly) {
        return {copy};
    }
    const DirectInsertion insertion = directInsertion(OutputMethod::normalized(settings.method), pasteMethod);
    QStringList route;
    if (insertion.requested) {
        route << QStringLiteral("insert");
    }
    if (insertion.only) {
        return route << copy;
    }
    for (const QString &method : orderedMethods(settings, pasteMethod)) {
        const QString step = pressesPasteKeys(method) ? QStringLiteral("press") : copy;
        if (!route.contains(step)) {
            route << step;
        }
    }
    return route;
}

TextDelivery::PasteNeed TextDelivery::pasteNeed(const OutputSettings &settings, PasteMethod pasteMethod)
{
    if (pasteMethod == PasteMethod::ClipboardOnly) {
        return PasteNeed::Nothing;
    }
#ifdef Q_OS_MACOS
    // ⌘V reaches another app only through accessibility.
    Q_UNUSED(settings)
    return PasteNeed::Accessibility;
#else
    if (pasteMethod == PasteMethod::DirectInsert) {
        return PasteNeed::Accessibility;
    }
    if (!OutputMethod::hasVirtualKeyboard) {
        return PasteNeed::Nothing;
    }
    return OutputMethod::normalized(settings.method) == QString::fromLatin1(OutputMethod::Ydotool)
        ? PasteNeed::VirtualKeyboard
        : PasteNeed::Either;
#endif
}

bool TextDelivery::wordsStayCopied(PasteNeed need, bool accessibility, bool virtualKeyboard)
{
    switch (need) {
    case PasteNeed::Accessibility:
        return !accessibility;
    case PasteNeed::VirtualKeyboard:
        return !virtualKeyboard;
    case PasteNeed::Either:
        return !accessibility && !virtualKeyboard;
    case PasteNeed::Nothing:
        break;
    }
    return false;
}

QStringList TextDelivery::orderedMethods(const OutputSettings &settings)
{
    return orderedMethods(settings, PasteMethod::StandardPaste);
}

QStringList TextDelivery::orderedMethods(const OutputSettings &settings, PasteMethod pasteMethod)
{
#if defined(SPEECHER_WITH_MAC)
    const QString method = OutputMethod::normalized(settings.method);
    if (method == QString::fromLatin1(OutputMethod::Automatic)) {
        QStringList methods;
        if (pasteMethod != PasteMethod::ClipboardOnly) {
            methods << QString::fromLatin1(OutputMethod::MacPaste);
        }
        methods << QString::fromLatin1(OutputMethod::QtClipboard);
        return methods;
    }
    if (method == QString::fromLatin1(OutputMethod::MacPaste)
        && pasteMethod == PasteMethod::ClipboardOnly) {
        return {QString::fromLatin1(OutputMethod::QtClipboard)};
    }
    // The Linux-only methods have no backend here; the factory returns nothing
    // for them and the clipboard copy deliver() already made is the net.
    return {method};
#elif defined(SPEECHER_WITH_WIN)
    const QString method = OutputMethod::normalized(settings.method);
    if (method == QString::fromLatin1(OutputMethod::Automatic)) {
        return pasteMethod == PasteMethod::ClipboardOnly
            ? QStringList{QString::fromLatin1(OutputMethod::QtClipboard)}
            : QStringList{QString::fromLatin1(OutputMethod::WinPaste),
                          QString::fromLatin1(OutputMethod::QtClipboard)};
    }
    if (method == QString::fromLatin1(OutputMethod::WinPaste)
        && pasteMethod == PasteMethod::ClipboardOnly) {
        return {QString::fromLatin1(OutputMethod::QtClipboard)};
    }
    return {method};
#else
    const QString method = OutputMethod::normalized(settings.method);
    if (method == QString::fromLatin1(OutputMethod::Automatic)) {
        QStringList methods;
        if (pasteMethod != PasteMethod::ClipboardOnly && settings.ydotoolEnabled) {
            methods << QString::fromLatin1(OutputMethod::Ydotool);
        }
        methods << QString::fromLatin1(OutputMethod::WlCopy)
                << QString::fromLatin1(OutputMethod::QtClipboard);
        return methods;
    }

    if (method == QString::fromLatin1(OutputMethod::Ydotool)
        && (!settings.ydotoolEnabled || pasteMethod == PasteMethod::ClipboardOnly)) {
        return {
            QString::fromLatin1(OutputMethod::WlCopy),
            QString::fromLatin1(OutputMethod::QtClipboard),
        };
    }
    return {method};
#endif
}

} // namespace speecher
