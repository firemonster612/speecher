#include "output/DeliveryNeeds.h"

#ifdef SPEECHER_WITH_WAYLAND
#include "output/WaylandClipboardProcess.h"
#include "output/WlClipboardDelivery.h"
#endif

namespace speecher {

bool clipboardNeedsHelpers()
{
#ifdef SPEECHER_WITH_WAYLAND
    return WlClipboardDelivery::isWaylandSession();
#else
    return false;
#endif
}

QStringList deliveryNeeds(DeliveryStep step, const DeliverySteps &steps, bool waylandClipboard)
{
    switch (step) {
    case DeliveryStep::Insert:
#ifdef Q_OS_WIN
        return {};
#else
        return {DeliveryNeed::Accessibility};
#endif
    case DeliveryStep::Press: {
        QStringList needs;
#if defined(Q_OS_MACOS)
        // ⌘V reaches another app only through the Accessibility grant.
        needs << DeliveryNeed::Accessibility;
#elif !defined(Q_OS_WIN)
        needs << DeliveryNeed::VirtualKeyboard;
#endif
        if (waylandClipboard) {
            needs << DeliveryNeed::Clipboard;
            if (!steps.keep) {
                needs << DeliveryNeed::ClipboardSave;
            }
        }
        return needs;
    }
    case DeliveryStep::Keep:
        if (!waylandClipboard) {
            return {};
        }
        return {steps.keep ? DeliveryNeed::Clipboard : DeliveryNeed::ClipboardSave};
    }
    return {};
}

bool clipboardHelperReady(const QString &need)
{
#ifdef SPEECHER_WITH_WAYLAND
    return need == DeliveryNeed::ClipboardSave ? WlClipboardDelivery::canSnapshot() : WlClipboardDelivery::isAvailable();
#else
    Q_UNUSED(need)
    return true;
#endif
}

bool clipboardCopiesFormatted()
{
#ifdef SPEECHER_WITH_WAYLAND
    return !clipboardNeedsHelpers() || !WaylandClipboardProcess::helperExecutable().isEmpty();
#else
    return true;
#endif
}

} // namespace speecher
