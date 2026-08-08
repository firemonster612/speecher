#include "output/TextDelivery.h"

#include "core/AppSettings.h"
#include "core/OutputMethod.h"
#include "output/ClipboardDelivery.h"
#include "output/QtClipboardDelivery.h"
#include "output/WlClipboardDelivery.h"
#include "output/YdotoolDelivery.h"

#include <memory>
#include <utility>

namespace speecher {

namespace {

class YdotoolBackend final : public DeliveryBackend {
public:
    explicit YdotoolBackend(PasteMethod pasteMethod)
        : m_pasteMethod(pasteMethod)
    {
    }

    bool deliver(const DeliveryContent &content, bool *htmlAvailable, QString *error) override
    {
        QString copyError;
        if (!ClipboardDelivery().copy(content, htmlAvailable, &copyError)) {
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
    PasteMethod m_pasteMethod = PasteMethod::StandardPaste;
};

class WlCopyBackend final : public DeliveryBackend {
public:
    bool deliver(const DeliveryContent &content, bool *htmlAvailable, QString *error) override
    {
        if (htmlAvailable) {
            *htmlAvailable = false;
        }
        return WlClipboardDelivery().copy(content.plainText, error);
    }
};

class QtClipboardBackend final : public DeliveryBackend {
public:
    bool deliver(const DeliveryContent &content, bool *htmlAvailable, QString *error) override
    {
        if (htmlAvailable) {
            *htmlAvailable = content.html.has_value();
        }
        return QtClipboardDelivery().copy(content, error);
    }
};

std::unique_ptr<DeliveryBackend> defaultBackendFactory(const QString &method,
                                                       const OutputSettings &settings,
                                                       PasteMethod pasteMethod)
{
    Q_UNUSED(settings)
    if (method == QString::fromLatin1(OutputMethod::Ydotool)) {
        return std::make_unique<YdotoolBackend>(pasteMethod);
    }
    if (method == QString::fromLatin1(OutputMethod::WlCopy)) {
        return std::make_unique<WlCopyBackend>();
    }
    if (method == QString::fromLatin1(OutputMethod::QtClipboard)) {
        return std::make_unique<QtClipboardBackend>();
    }
    return nullptr;
}

} // namespace

TextDelivery::TextDelivery(QObject *parent)
    : TextDeliveryAdapter(parent)
    , m_backendFactory(defaultBackendFactory)
{
}

TextDelivery::TextDelivery(TargetProvider *targetProvider, QObject *parent)
    : TextDeliveryAdapter(parent)
    , m_backendFactory(defaultBackendFactory)
    , m_targetProvider(targetProvider)
{
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

DeliveryResult TextDelivery::deliver(const OutputSettings &settings,
                                     const DeliveryContent &content,
                                     const Target &target)
{
    PasteMethod pasteMethod = PasteMethod::ClipboardOnly;
    if (target.hasIdentity() && !target.secure) {
        pasteMethod = resolvePasteRule(settings.pasteRules, target).method;
    }
    const bool directInsertRequested = pasteMethod == PasteMethod::DirectInsert;
    const bool targetFocused = pasteMethod != PasteMethod::ClipboardOnly
        && m_targetProvider
        && m_targetProvider->stillFocused(target);
    if (directInsertRequested) {
        if (m_targetProvider && m_targetProvider->canInsertText(target)) {
            WlClipboardSnapshot previousClipboard;
            const bool canRestoreClipboard = settings.restoreClipboardAfterTyping
                && WlClipboardDelivery::canSnapshot()
                && WlClipboardDelivery::capture(&previousClipboard);
            bool htmlAvailable = false;
            QString copyError;
            if (!ClipboardDelivery().copy(content, &htmlAvailable, &copyError)) {
                return {
                    false,
                    DeliveryReceipt::None,
                    false,
                    copyError.isEmpty() ? QStringLiteral("Could not copy text before direct insertion") : copyError,
                };
            }
            QString insertionError;
            if (m_targetProvider->insertText(target, content.plainText, &insertionError)) {
                const bool verified = m_targetProvider->verifyInsertion(target, content.plainText);
                QString restoreError;
                const bool restored = !verified
                    || !canRestoreClipboard
                    || WlClipboardDelivery::restore(previousClipboard, &restoreError);
                QString message = verified
                    ? QStringLiteral("Verified in Target")
                    : QStringLiteral("Accepted by Target");
                if (verified && canRestoreClipboard && !restored) {
                    message += QStringLiteral("; clipboard kept because it could not be restored");
                }
                const bool downgraded = content.html.has_value() && !htmlAvailable;
                return {
                    true,
                    verified ? DeliveryReceipt::VerifiedInTarget : DeliveryReceipt::AcceptedByTarget,
                    downgraded,
                    downgraded ? message + QStringLiteral(" as plain text") : message,
                };
            }
            return {
                true,
                DeliveryReceipt::Copied,
                content.html.has_value() && !htmlAvailable,
                insertionError.isEmpty()
                    ? QStringLiteral("Copied")
                    : QStringLiteral("Copied; direct insertion was rejected"),
            };
        }
        pasteMethod = PasteMethod::ClipboardOnly;
    } else if (pasteMethod != PasteMethod::ClipboardOnly && !targetFocused) {
        pasteMethod = PasteMethod::ClipboardOnly;
    }

    WlClipboardSnapshot previousClipboard;
    const bool canRestoreClipboard = pasteMethod != PasteMethod::ClipboardOnly
        && settings.restoreClipboardAfterTyping
        && WlClipboardDelivery::canSnapshot()
        && WlClipboardDelivery::capture(&previousClipboard);

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
            const bool copied = method == QString::fromLatin1(OutputMethod::WlCopy)
                || method == QString::fromLatin1(OutputMethod::QtClipboard);
            const bool downgraded = content.html.has_value() && !htmlAvailable;
            const bool verified = !copied
                && m_targetProvider
                && m_targetProvider->verifyInsertion(target, content.plainText);
            QString restoreError;
            const bool restored = !verified
                || !canRestoreClipboard
                || WlClipboardDelivery::restore(previousClipboard, &restoreError);
            const QString message = copied
                ? QStringLiteral("Copied")
                : verified ? QStringLiteral("Verified in Target") : QStringLiteral("Input sent");
            const QString finalMessage = verified && canRestoreClipboard && !restored
                ? message + QStringLiteral("; clipboard kept because it could not be restored")
                : message;
            return {true,
                    copied ? DeliveryReceipt::Copied
                           : verified ? DeliveryReceipt::VerifiedInTarget : DeliveryReceipt::InputSent,
                    downgraded,
                    downgraded ? finalMessage + QStringLiteral(" as plain text") : finalMessage};
        }
        if (firstError.isEmpty()) {
            firstError = error;
        }
    }
    return {false,
            DeliveryReceipt::None,
            false,
            firstError.isEmpty() ? QStringLiteral("No output method is available") : firstError};
}

QStringList TextDelivery::orderedMethods(const OutputSettings &settings)
{
    return orderedMethods(settings, PasteMethod::StandardPaste);
}

QStringList TextDelivery::orderedMethods(const OutputSettings &settings, PasteMethod pasteMethod)
{
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
}

} // namespace speecher
