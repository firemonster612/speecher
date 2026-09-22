#include "platform/RoutingShortcutBinder.h"

namespace speecher {

RoutingShortcutBinder::RoutingShortcutBinder(GlobalShortcutBinder *combination,
                                             GlobalShortcutBinder *singleKey,
                                             QObject *parent)
    : GlobalShortcutBinder(parent)
    , m_combination(combination)
    , m_singleKey(singleKey)
{
    m_combination->setParent(this);
    m_singleKey->setParent(this);
    for (GlobalShortcutBinder *binder : {m_combination, m_singleKey}) {
        // Only the binder that owns the binding may start dictation: a backend
        // whose registration lingers (or arrives late) must not fire alongside
        // the selected one. A release is forwarded from both unconditionally:
        // it must always be able to clear a latched activation even if
        // ownership flipped mid-hold.
        connect(binder, &GlobalShortcutBinder::activated, this, [this, binder] {
            const bool singleKeyOwns = m_singleKey->shortcut().isSingleKey();
            if (binder == (singleKeyOwns ? m_singleKey : m_combination)) {
                emit activated();
            }
        });
        connect(binder, &GlobalShortcutBinder::deactivated, this, &GlobalShortcutBinder::deactivated);
        connect(binder, &GlobalShortcutBinder::bindingChanged, this, &GlobalShortcutBinder::bindingChanged);
        connect(binder, &GlobalShortcutBinder::supportChanged, this, &GlobalShortcutBinder::supportChanged);
    }
    // The desktop chooser binds a combination on its own; when it does, the
    // single key it replaces stops being watched.
    connect(m_combination, &GlobalShortcutBinder::registrationFinished, this,
            [this](bool bound, const QString &detail) {
                if (bound && m_singleKey->shortcut().isSingleKey()) {
                    m_singleKey->setShortcut({});
                }
                emit registrationFinished(bound, detail);
            });
    // A single-key backend can also bind on its own, later: the mac binder
    // rebinds when its Accessibility grant arrives, the keywatch binder when
    // its helper gets installed. bind() may have registered the combination
    // in the meantime, and there is one binding, so let the combination go.
    connect(m_singleKey, &GlobalShortcutBinder::bindingChanged, this, [this] {
        if (m_singleKey->shortcut().isSingleKey()) {
            m_combination->removeRegistration();
        }
    });
}

bool RoutingShortcutBinder::supported() const
{
    return m_combination->supported();
}

bool RoutingShortcutBinder::supportKnown() const
{
    return m_combination->supportKnown();
}

bool RoutingShortcutBinder::usesDesktopShortcutChooser() const
{
    return m_combination->usesDesktopShortcutChooser();
}

QString RoutingShortcutBinder::unsupportedReason() const
{
    return m_combination->unsupportedReason();
}

// A stored single key outranks whatever the desktop service still holds; the
// service is not bound then, so a stale registration cannot fire alongside.
void RoutingShortcutBinder::bind()
{
    m_singleKey->bind();
    if (m_singleKey->shortcut().isSingleKey()) {
        return;
    }
    m_combination->bind();
}

ShortcutBinding RoutingShortcutBinder::shortcut() const
{
    const ShortcutBinding single = m_singleKey->shortcut();
    return single.isSingleKey() ? single : m_combination->shortcut();
}

QString RoutingShortcutBinder::shortcutDisplay() const
{
    return m_singleKey->shortcut().isSingleKey() ? m_singleKey->shortcutDisplay()
                                                 : m_combination->shortcutDisplay();
}

QString RoutingShortcutBinder::unsupportedBindingReason(const ShortcutBinding &binding) const
{
    return binding.isSingleKey() ? m_singleKey->unsupportedBindingReason(binding)
                                 : m_combination->unsupportedBindingReason(binding);
}

bool RoutingShortcutBinder::setShortcut(const ShortcutBinding &shortcut, QString *error)
{
    if (shortcut.isSingleKey()) {
        // Success emits bindingChanged, whose handler above lets the
        // desktop-service registration go.
        return m_singleKey->setShortcut(shortcut, error);
    }
    if (!m_combination->setShortcut(shortcut, error)) {
        return false;
    }
    m_singleKey->setShortcut({});
    return true;
}

void RoutingShortcutBinder::suspend()
{
    m_combination->suspend();
    m_singleKey->suspend();
}

QString RoutingShortcutBinder::resume()
{
    const QString error = m_combination->resume();
    m_singleKey->resume();
    return error;
}

bool RoutingShortcutBinder::removeRegistration(QString *error)
{
    m_singleKey->setShortcut({});
    return m_combination->removeRegistration(error);
}

void RoutingShortcutBinder::registerShortcut()
{
    m_combination->registerShortcut();
}

} // namespace speecher
