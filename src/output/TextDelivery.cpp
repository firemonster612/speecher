#include "output/TextDelivery.h"

#include "core/AppSettings.h"
#include "core/OutputMethod.h"
#ifdef SPEECHER_WITH_WAYLAND
#include "output/YdotoolDelivery.h"
#endif
#ifdef SPEECHER_WITH_MAC
#include "output/mac/MacPasteDelivery.h"
#endif

#include <QEventLoop>
#include <QTimer>

#include <memory>
#include <utility>

namespace speecher {

namespace {

constexpr int clipboardRestoreDelayMs = 750;

#ifdef SPEECHER_WITH_WAYLAND
class YdotoolBackend final : public DeliveryBackend {
public:
    YdotoolBackend(ClipboardDelivery *clipboardDelivery, PasteMethod pasteMethod)
        : m_clipboardDelivery(clipboardDelivery)
        , m_pasteMethod(pasteMethod)
    {
    }

    bool deliver(const DeliveryContent &content, bool *htmlAvailable, QString *error) override
    {
        QString copyError;
        if (!m_clipboardDelivery->copy(content, htmlAvailable, &copyError)) {
            if (error) {
                *error = copyError.isEmpty()
                    ? QStringLiteral("Could not copy text before ydotool paste")
                    : QStringLiteral("Could not copy text before ydotool paste: %1").arg(copyError);
            }
            return false;
        }

        QString pasteError;
        if (!YdotoolDelivery().pasteFromClipboard(content.plainText, m_pasteMethod, &pasteError)) {
            if (error) {
                *error = pasteError;
            }
            return false;
        }
        return true;
    }

private:
    ClipboardDelivery *m_clipboardDelivery = nullptr;
    PasteMethod m_pasteMethod = PasteMethod::StandardPaste;
};

class WlCopyBackend final : public DeliveryBackend {
public:
    explicit WlCopyBackend(ClipboardDelivery *clipboardDelivery)
        : m_clipboardDelivery(clipboardDelivery)
    {
    }

    bool deliver(const DeliveryContent &content, bool *htmlAvailable, QString *error) override
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
    explicit MacPasteBackend(ClipboardDelivery *clipboardDelivery)
        : m_clipboardDelivery(clipboardDelivery)
    {
    }

    bool deliver(const DeliveryContent &content, bool *htmlAvailable, QString *error) override
    {
        QString copyError;
        if (!m_clipboardDelivery->copy(content, htmlAvailable, &copyError)) {
            if (error) {
                *error = copyError.isEmpty()
                    ? QStringLiteral("Could not copy text before keyboard paste")
                    : QStringLiteral("Could not copy text before keyboard paste: %1").arg(copyError);
            }
            return false;
        }
        return MacPasteDelivery().paste(error);
    }

private:
    ClipboardDelivery *m_clipboardDelivery = nullptr;
};
#endif // SPEECHER_WITH_MAC

class QtClipboardBackend final : public DeliveryBackend {
public:
    explicit QtClipboardBackend(ClipboardDelivery *clipboardDelivery)
        : m_clipboardDelivery(clipboardDelivery)
    {
    }

    bool deliver(const DeliveryContent &content, bool *htmlAvailable, QString *error) override
    {
        return m_clipboardDelivery->copyQt(content, htmlAvailable, error);
    }

private:
    ClipboardDelivery *m_clipboardDelivery = nullptr;
};

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
            return std::make_unique<YdotoolBackend>(&m_clipboardDelivery, pasteMethod);
        }
        if (method == QString::fromLatin1(OutputMethod::WlCopy)) {
            return std::make_unique<WlCopyBackend>(&m_clipboardDelivery);
        }
#endif
#ifdef SPEECHER_WITH_MAC
        if (method == QString::fromLatin1(OutputMethod::MacPaste)) {
            return std::make_unique<MacPasteBackend>(&m_clipboardDelivery);
        }
#endif
        if (method == QString::fromLatin1(OutputMethod::QtClipboard)) {
            return std::make_unique<QtClipboardBackend>(&m_clipboardDelivery);
        }
        return nullptr;
    };
}

DeliveryResult TextDelivery::deliver(const OutputSettings &settings,
                                     const DeliveryContent &content,
                                     const Target &target)
{
    const PasteRule pasteRule = resolvePasteRule(settings.pasteRules, target);
    const bool trackableTarget = target.hasIdentity() || target.accessible;
    const bool currentFocusFallback = !trackableTarget
        && pasteRule.scope == PasteRuleScope::Global
        && (pasteRule.method == PasteMethod::StandardPaste
            || pasteRule.method == PasteMethod::TerminalPaste);
    PasteMethod pasteMethod = PasteMethod::ClipboardOnly;
    if (!target.secure && (trackableTarget || currentFocusFallback)) {
        pasteMethod = pasteRule.method;
    }
    const QString outputMethod = OutputMethod::normalized(settings.method);
    const bool ruleDirectInsert = pasteMethod == PasteMethod::DirectInsert;
    const bool outputDirectInsert = pasteMethod != PasteMethod::ClipboardOnly
        && outputMethod == QString::fromLatin1(OutputMethod::DirectInsert);
    const bool automaticDirectInsert = pasteMethod != PasteMethod::ClipboardOnly
        && outputMethod == QString::fromLatin1(OutputMethod::Automatic);
    const bool directInsertRequested = ruleDirectInsert
        || outputDirectInsert
        || automaticDirectInsert;
    const bool directInsertOnly = ruleDirectInsert || outputDirectInsert;
    const bool targetFocused = pasteMethod != PasteMethod::ClipboardOnly
        && (currentFocusFallback
            || (m_targetProvider && m_targetProvider->stillFocused(target)));

    QString clipboardWarning;
    ClipboardSnapshot previousClipboard;
    bool canRestoreClipboard = false;
    if (pasteMethod != PasteMethod::ClipboardOnly && settings.restoreClipboardAfterTyping) {
        if (!m_clipboardDelivery.canSnapshot()) {
            clipboardWarning = QStringLiteral("Previous clipboard could not be saved because snapshots are unavailable");
        } else {
            QString captureError;
            canRestoreClipboard = m_clipboardDelivery.capture(&previousClipboard, &captureError);
            if (!canRestoreClipboard) {
                clipboardWarning = captureError.isEmpty()
                    ? QStringLiteral("Previous clipboard could not be saved")
                    : QStringLiteral("Previous clipboard could not be saved: %1").arg(captureError);
            }
        }
    }
    const auto withClipboardWarning = [&clipboardWarning](QString message) {
        if (!clipboardWarning.isEmpty()) {
            message += QStringLiteral("; ") + clipboardWarning;
        }
        return message;
    };
    bool initiallyHtmlAvailable = false;
    QString initialCopyError;
    if (!m_clipboardDelivery.copy(content, &initiallyHtmlAvailable, &initialCopyError)) {
        return {
            false,
            DeliveryReceipt::None,
            false,
            initialCopyError.isEmpty() ? QStringLiteral("Could not copy the transcription")
                                       : initialCopyError,
        };
    }

    QString insertionError;
    if (directInsertRequested
        && m_targetProvider
        && m_targetProvider->canInsertText(target)
        && m_targetProvider->insertText(target, content.plainText, &insertionError)) {
        const bool verified = m_targetProvider->verifyInsertion(target, content.plainText);
        QString restoreError;
        const bool restored = !verified
            || !canRestoreClipboard
            || m_clipboardDelivery.restore(previousClipboard, &restoreError);
        QString message = verified
            ? QStringLiteral("Verified in Target")
            : QStringLiteral("Accepted by Target");
        if (verified && canRestoreClipboard && !restored) {
            clipboardWarning = restoreError.isEmpty()
                ? QStringLiteral("Previous clipboard could not be restored")
                : QStringLiteral("Previous clipboard could not be restored: %1").arg(restoreError);
        }
        const bool downgraded = content.html.has_value() && !initiallyHtmlAvailable;
        return {
            true,
            verified ? DeliveryReceipt::VerifiedInTarget : DeliveryReceipt::AcceptedByTarget,
            downgraded,
            withClipboardWarning(
                downgraded ? message + QStringLiteral(" as plain text") : message),
        };
    }
    if (directInsertOnly) {
        if (!insertionError.isEmpty()) {
            return {
                true,
                DeliveryReceipt::Copied,
                content.html.has_value() && !initiallyHtmlAvailable,
                withClipboardWarning(QStringLiteral("Copied; direct insertion was rejected")),
            };
        }
        pasteMethod = PasteMethod::ClipboardOnly;
    }
    if (pasteMethod != PasteMethod::ClipboardOnly && !targetFocused) {
        pasteMethod = PasteMethod::ClipboardOnly;
    }

    if (pasteMethod == PasteMethod::ClipboardOnly) {
        const bool downgraded = content.html.has_value() && !initiallyHtmlAvailable;
        return {
            true,
            DeliveryReceipt::Copied,
            downgraded,
            withClipboardWarning(
                downgraded ? QStringLiteral("Copied as plain text") : QStringLiteral("Copied")),
        };
    }

    QString firstError;
    for (const QString &method : orderedMethods(settings, pasteMethod)) {
        std::unique_ptr<DeliveryBackend> backend = m_backendFactory
            ? m_backendFactory(method, settings, pasteMethod)
            : nullptr;
        if (!backend) {
            continue;
        }

        QString error;
        bool htmlAvailable = false;
        if (backend->deliver(content, &htmlAvailable, &error)) {
            const bool virtualKeyboardInput = method == QString::fromLatin1(OutputMethod::Ydotool)
                || method == QString::fromLatin1(OutputMethod::MacPaste);
            const bool copied = method == QString::fromLatin1(OutputMethod::WlCopy)
                || method == QString::fromLatin1(OutputMethod::QtClipboard);
            const bool downgraded = content.html.has_value() && !htmlAvailable;
            const bool verified = !copied
                && m_targetProvider
                && m_targetProvider->verifyInsertion(target, content.plainText);
            bool restoredClipboard = false;
            if (virtualKeyboardInput && canRestoreClipboard) {
                if (!verified) {
                    QEventLoop waitForClipboardConsumer;
                    QTimer::singleShot(clipboardRestoreDelayMs,
                                       &waitForClipboardConsumer,
                                       &QEventLoop::quit);
                    waitForClipboardConsumer.exec(QEventLoop::ExcludeUserInputEvents);
                }
                QString restoreError;
                restoredClipboard = m_clipboardDelivery.restore(previousClipboard, &restoreError);
                if (!restoredClipboard) {
                    clipboardWarning = restoreError.isEmpty()
                        ? QStringLiteral("Previous clipboard could not be restored")
                        : QStringLiteral("Previous clipboard could not be restored: %1").arg(restoreError);
                }
            }
            const QString message = copied
                ? QStringLiteral("Copied")
                : virtualKeyboardInput
                    ? restoredClipboard
                        ? QStringLiteral("Input sent")
                        : QStringLiteral("Copied • Input sent")
                    : verified ? QStringLiteral("Verified in Target") : QStringLiteral("Input sent");
            return {true,
                    copied ? DeliveryReceipt::Copied
                           : verified ? DeliveryReceipt::VerifiedInTarget : DeliveryReceipt::InputSent,
                    downgraded,
                    withClipboardWarning(
                        downgraded && !virtualKeyboardInput
                            ? message + QStringLiteral(" as plain text")
                            : message)};
        }
        if (firstError.isEmpty()) {
            firstError = error;
        }
        // A failed paste already left the text on the clipboard, so the later
        // clipboard methods would only re-report what the caller already has.
        if (method == QString::fromLatin1(OutputMethod::Ydotool)
            || method == QString::fromLatin1(OutputMethod::MacPaste)) {
            break;
        }
    }
    const bool downgraded = content.html.has_value() && !initiallyHtmlAvailable;
    return {
        true,
        DeliveryReceipt::Copied,
        downgraded,
        withClipboardWarning(
            firstError.isEmpty()
                ? (downgraded ? QStringLiteral("Copied as plain text") : QStringLiteral("Copied"))
                : QStringLiteral("Copied; insertion failed: %1").arg(firstError)),
    };
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
