#pragma once

#include "dictation/DictationPorts.h"
#include "platform/GlobalShortcutBinder.h"

#include <QList>

namespace speecher {

// Text delivery injects keystrokes, and a single-key binder watching the
// injected key would take them for the user's finger. Suspending exactly for
// the duration of the delivery call closes that loop: the single-key binders
// gate events while suspended and drain their sources before the suspension
// lifts, so nothing injected leaks through after resume either. Every Global
// Shortcut is suspended: an injected key can match the cancel shortcut as
// easily as the dictation one.
class ShortcutSuspendingDelivery : public TextDeliveryAdapter {
    Q_OBJECT

public:
    ShortcutSuspendingDelivery(TextDeliveryAdapter *inner,
                               QList<GlobalShortcutBinder *> binders,
                               QObject *parent = nullptr)
        : TextDeliveryAdapter(parent)
        , m_inner(inner)
        , m_binders(std::move(binders))
    {
    }

    DeliveryResult deliver(const OutputSettings &settings,
                           const DeliveryContent &content,
                           const Target &target) override
    {
        for (GlobalShortcutBinder *binder : m_binders) {
            binder->suspend();
        }
        const DeliveryResult result = m_inner->deliver(settings, content, target);
        for (GlobalShortcutBinder *binder : m_binders) {
            binder->resume();
        }
        return result;
    }

private:
    TextDeliveryAdapter *m_inner;
    QList<GlobalShortcutBinder *> m_binders;
};

} // namespace speecher
