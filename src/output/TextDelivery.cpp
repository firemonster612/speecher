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

    bool deliver(const DeliveryContent &content,
                 const std::function<bool()> &clearToInject,
                 bool *,
                 QString *error) override
    {
        return YdotoolDelivery().pasteFromClipboard(content.plainText, m_pasteMethod, clearToInject, error);
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

    bool deliver(const DeliveryContent &content,
                 const std::function<bool()> &,
                 bool *htmlAvailable,
                 QString *error) override
    {
        return m_clipboardDelivery->copyWayland(content, htmlAvailable, error);
    }

private:
    ClipboardDelivery *m_clipboardDelivery = nullptr;
};
#endif // SPEECHER_WITH_WAYLAND

#ifdef SPEECHER_WITH_MAC
class MacPasteBackend final : public DeliveryBackend {
public:
    bool deliver(const DeliveryContent &,
                 const std::function<bool()> &clearToInject,
                 bool *,
                 QString *error) override
    {
        return MacPasteDelivery().paste(clearToInject, error);
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

    bool deliver(const DeliveryContent &,
                 const std::function<bool()> &clearToInject,
                 bool *,
                 QString *error) override
    {
        return WinPasteDelivery().paste(m_pasteMethod, clearToInject, error);
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

    bool deliver(const DeliveryContent &content,
                 const std::function<bool()> &,
                 bool *htmlAvailable,
                 QString *error) override
    {
        return m_clipboardDelivery->copyQt(content, htmlAvailable, error);
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
// is verified, and what the receipt says about it.
class TextDelivery::PreviousClipboard {
public:
    // Saves the clipboard when wanted, or notes why it could not.
    PreviousClipboard(ClipboardDelivery &clipboard, bool wanted)
        : m_clipboard(clipboard)
    {
        if (!wanted) {
            return;
        }
        if (!m_clipboard.canSnapshot()) {
            m_warning = QStringLiteral("Previous clipboard could not be saved because snapshots are unavailable");
            return;
        }
        QString captureError;
        m_saved = m_clipboard.capture(&m_snapshot, &captureError);
        if (!m_saved) {
            m_warning = captureError.isEmpty()
                ? QStringLiteral("Previous clipboard could not be saved")
                : QStringLiteral("Previous clipboard could not be saved: %1").arg(captureError);
        }
    }

    // Puts the saved clipboard back after a verified paste. False when nothing
    // was saved, or when it could not be put back, which the receipt then says.
    bool restore()
    {
        if (!m_saved) {
            return false;
        }
        QString error;
        bool keptNewerCopy = false;
        const bool ok = m_clipboard.restore(m_snapshot, &error, true, &keptNewerCopy);
        if (ok && keptNewerCopy) {
            m_note = QStringLiteral("Previous clipboard kept as-is.");
        }
        if (!ok) {
            m_warning = error.isEmpty()
                ? QStringLiteral("Previous clipboard could not be restored")
                : QStringLiteral("Previous clipboard could not be restored: %1").arg(error);
        }
        return ok;
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

void TextDelivery::deliver(const OutputSettings &settings,
                           const DeliveryContent &content,
                           const Target &target)
{
    m_requests.append({settings, content, target});
    // A delivery waiting in a nested event loop, to check focus or verify an
    // insertion, can be asked for another; that one waits its turn, so the
    // answers keep the order of the requests.
    if (m_requests.size() > 1) {
        return;
    }
    while (!m_requests.isEmpty()) {
        const Request request = m_requests.first();
        const DeliveryResult result = deliverNow(request.settings, request.content, request.target);
        QMetaObject::invokeMethod(this, [this, result] { emit delivered(result); }, Qt::QueuedConnection);
        m_requests.removeFirst();
    }
}

DeliveryResult TextDelivery::deliverNow(const OutputSettings &settings,
                                        const DeliveryContent &content,
                                        const Target &target)
{
    const PasteChoice choice = choosePasteMethod(settings, target);
    PasteMethod pasteMethod = choice.method;
    const DirectInsertion insertion = directInsertion(OutputMethod::normalized(settings.method), pasteMethod);
    const TargetProvider::FocusMatch focus = m_targetProvider
        ? m_targetProvider->focusMatch(target)
        : TargetProvider::FocusMatch::Different;
    const bool targetFocused = pasteMethod != PasteMethod::ClipboardOnly
        && (choice.currentFocusFallback || focus == TargetProvider::FocusMatch::Same);

    PreviousClipboard previousClipboard(m_clipboardDelivery,
                                        pasteMethod != PasteMethod::ClipboardOnly
                                            && settings.restoreClipboardAfterTyping);
    bool initiallyHtmlAvailable = false;
    QString initialCopyError;
    const bool copiedInitially = m_clipboardDelivery.copy(content, &initiallyHtmlAvailable, &initialCopyError);
    const bool copiedAsPlainText = content.html.has_value() && !initiallyHtmlAvailable;

    QString insertionError;
    if (insertion.requested
        && m_targetProvider
        && m_targetProvider->canInsertText(target)
        && m_targetProvider->insertText(target, content.plainText, &insertionError)) {
        const bool verified = m_targetProvider->verifyInsertion(target, content.plainText);
        if (copiedInitially && verified) {
            previousClipboard.restore();
        }
        const QString message = verified
            ? QStringLiteral("Pasted")
            : copiedInitially
                ? QStringLiteral("Could not confirm insertion. Your dictation is on the clipboard.")
                : QStringLiteral("Accepted by Target; clipboard copy failed: %1").arg(initialCopyError);
        return {
            true,
            verified ? DeliveryReceipt::VerifiedInTarget : DeliveryReceipt::AcceptedByTarget,
            copiedAsPlainText,
            previousClipboard.annotate(
                copiedAsPlainText ? message + QStringLiteral(" as plain text") : message),
        };
    }
    if (!copiedInitially) {
        return {false, DeliveryReceipt::None, false,
                previousClipboard.annotate(initialCopyError.isEmpty()
                    ? QStringLiteral("Could not copy or insert the transcription")
                    : QStringLiteral("Could not copy or insert the transcription: %1").arg(initialCopyError))};
    }
    if (insertion.only) {
        if (!insertionError.isEmpty()) {
            return {
                true,
                DeliveryReceipt::Copied,
                copiedAsPlainText,
                previousClipboard.annotate(QStringLiteral("Copied; direct insertion was rejected")),
            };
        }
        pasteMethod = PasteMethod::ClipboardOnly;
    }
    QString focusWarning;
    if (pasteMethod != PasteMethod::ClipboardOnly && !targetFocused) {
        if (target.compositorActive) {
            // A retitled window and a second window of the same program look
            // alike from here, and one of them is not a window change at all,
            // so that reading is kept out of the message.
            focusWarning = focus == TargetProvider::FocusMatch::TitleChanged
                ? QStringLiteral("The window title changed. Your dictation is on the clipboard.")
                : QStringLiteral("The active window changed or could not be verified. Your dictation is on the clipboard.");
        }
        pasteMethod = PasteMethod::ClipboardOnly;
    }

    if (pasteMethod == PasteMethod::ClipboardOnly) {
        return {
            true,
            DeliveryReceipt::Copied,
            copiedAsPlainText,
            previousClipboard.annotate(
                !focusWarning.isEmpty() ? focusWarning
                    : copiedAsPlainText ? QStringLiteral("Copied as plain text") : QStringLiteral("Copied")),
        };
    }

    QString firstError;
    if (std::optional<DeliveryResult> pasted = pasteThroughBackends(
            settings, content, target, choice, copiedAsPlainText, previousClipboard, &firstError)) {
        return *pasted;
    }
    return {
        true,
        DeliveryReceipt::Copied,
        copiedAsPlainText,
        previousClipboard.annotate(
            firstError.isEmpty()
                ? (copiedAsPlainText ? QStringLiteral("Copied as plain text") : QStringLiteral("Copied"))
                : QStringLiteral("Copied; insertion failed: %1").arg(firstError)),
    };
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

std::optional<DeliveryResult> TextDelivery::pasteThroughBackends(const OutputSettings &settings,
                                                                 const DeliveryContent &content,
                                                                 const Target &target,
                                                                 const PasteChoice &choice,
                                                                 bool copiedAsPlainText,
                                                                 PreviousClipboard &previousClipboard,
                                                                 QString *firstError)
{
    for (const QString &method : orderedMethods(settings, choice.method)) {
        std::unique_ptr<DeliveryBackend> backend = m_backendFactory
            ? m_backendFactory(method, settings, choice.method)
            : nullptr;
        if (!backend) {
            continue;
        }

        QString error;
        bool htmlAvailable = false;
        const bool virtualKeyboardInput = pressesPasteKeys(method);
        // currentFocusFallback is the blind paste into whatever is in front:
        // there is no target to still be focused on.
        const bool focusGated = virtualKeyboardInput && m_targetProvider && !choice.currentFocusFallback;
        // The focus gate belongs here, immediately before the keystrokes go
        // out: preparePaste marks the edit for verification, it does not
        // decide whether to paste.
        if (focusGated) {
            const QString refusal = preparePasteInFocus(target);
            if (!refusal.isEmpty()) {
                *firstError = refusal;
                return std::nullopt;
            }
        }
        // The keyboard backends block again after the check above (modifier
        // release), so they get a last-moment gate to call immediately before
        // the keystroke goes out.
        const std::function<bool()> clearToInject = focusGated
            ? std::function<bool()>([this, &target] {
                  return m_targetProvider->focusMatch(target) == TargetProvider::FocusMatch::Same;
              })
            : std::function<bool()>();
        if (backend->deliver(content, clearToInject, &htmlAvailable, &error)) {
            const bool copied = method == QString::fromLatin1(OutputMethod::WlCopy)
                || method == QString::fromLatin1(OutputMethod::QtClipboard);
            // A keyboard backend pastes the copy deliver() made.
            const bool downgraded = virtualKeyboardInput ? copiedAsPlainText
                                                         : content.html.has_value() && !htmlAvailable;
            const bool verified = !copied
                && m_targetProvider
                && m_targetProvider->verifyInsertion(target, content.plainText);
            const bool restoredClipboard = virtualKeyboardInput && verified && previousClipboard.restore();
            // A compositor-fallback target has no accessible node, so there is
            // nothing to read back: the paste can never be confirmed. Saying
            // "could not confirm" on every such dictation reads like a failure
            // when nothing went wrong.
            const bool verifiable = !target.compositorActive;
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
            return DeliveryResult{true,
                                  copied ? DeliveryReceipt::Copied
                                         : verified ? DeliveryReceipt::VerifiedInTarget : DeliveryReceipt::InputSent,
                                  downgraded,
                                  previousClipboard.annotate(
                                      downgraded && !virtualKeyboardInput
                                          ? message + QStringLiteral(" as plain text")
                                          : message)};
        }
        if (firstError->isEmpty()) {
            *firstError = error;
        }
        // A failed paste already left the text on the clipboard, so the later
        // clipboard methods would only re-report what the caller already has.
        if (virtualKeyboardInput) {
            return std::nullopt;
        }
    }
    return std::nullopt;
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
