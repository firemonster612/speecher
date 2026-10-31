#pragma once

#include "dictation/DictationPorts.h"
#include "platform/GlobalShortcutBinder.h"

#include <QList>

namespace speecher {

// Text delivery injects keystrokes, and a single-key binder watching the
// injected key would take them for the user's finger. Suspending from the
// deliver() call until its delivered() closes that loop: the single-key binders
// gate events while suspended and drain their sources before the suspension
// lifts, so nothing injected leaks through after resume either. Every Global
// Shortcut is suspended: an injected key can match the cancel shortcut as
// easily as the dictation one. Deliveries that overlap share one suspension,
// since the binders do not count theirs: it lifts with the last answer.
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
        connect(m_inner, &TextDeliveryAdapter::delivered, this, [this](const DeliveryResult &result) {
            if (--m_unanswered == 0) {
                for (GlobalShortcutBinder *binder : m_binders) {
                    binder->resumeAfterDelivery();
                }
            }
            emit delivered(result);
        });
    }

    void deliver(const OutputSettings &settings,
                 const DeliveryContent &content,
                 const Target &target) override
    {
        if (m_unanswered++ == 0) {
            for (GlobalShortcutBinder *binder : m_binders) {
                binder->suspendForDelivery();
            }
        }
        m_inner->deliver(settings, content, target);
    }

private:
    TextDeliveryAdapter *m_inner;
    QList<GlobalShortcutBinder *> m_binders;
    int m_unanswered = 0;
};

} // namespace speecher
