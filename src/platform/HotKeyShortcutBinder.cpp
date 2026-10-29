#include "platform/HotKeyShortcutBinder.h"

#include "core/SettingsStore.h"

#include <QDebug>

namespace speecher {
namespace {

// The stored value can be a single key, which belongs to the single-key
// binder and leaves this one with the default.
QKeySequence savedShortcut(const SettingsStore &settings, const GlobalShortcutAction &action)
{
    const QKeySequence stored = settings.globalShortcut(action.role).combination();
    return stored.isEmpty() ? action.defaultShortcut : stored;
}

} // namespace

HotKeyShortcutBinder::HotKeyShortcutBinder(SettingsStore *settings,
                                           GlobalShortcutAction action,
                                           QObject *parent)
    : GlobalShortcutBinder(std::move(action), parent)
    , m_settings(settings)
    , m_shortcut(savedShortcut(*settings, this->action()))
{
}

bool HotKeyShortcutBinder::supported() const
{
    return true;
}

QString HotKeyShortcutBinder::unsupportedReason() const
{
    return {};
}

void HotKeyShortcutBinder::bind()
{
    if (m_shortcut.isEmpty()) {
        return;
    }
    if (m_suspensionCount > 0) {
        m_resumeBinding = true;
        return;
    }
    QString error;
    if (!registerKeys(m_shortcut, &error)) {
        qWarning().noquote() << "Could not register the Global Shortcut:" << error;
    }
}

ShortcutBinding HotKeyShortcutBinder::shortcut() const
{
    return m_shortcut;
}

bool HotKeyShortcutBinder::setShortcut(const ShortcutBinding &shortcut, QString *error)
{
    const QString reason = unsupportedBindingReason(shortcut);
    if (!reason.isEmpty()) {
        if (error) {
            *error = reason;
        }
        return false;
    }
    if (shortcut.isEmpty()) {
        removeRegistration();
        m_shortcut = {};
        m_settings->forgetGlobalShortcutCombination(action().role);
        emit bindingChanged();
        return true;
    }
    if (!registerKeys(shortcut.combination(), error)) {
        return false;
    }
    if (m_suspensionCount > 0) {
        // Validate conflicts now, but leave keys available to other recorders.
        m_resumeBinding = true;
        unregisterKeys();
    }
    m_shortcut = shortcut.combination();
    m_settings->setGlobalShortcut(action().role, m_shortcut);
    emit bindingChanged();
    return true;
}

void HotKeyShortcutBinder::suspend()
{
    if (m_suspensionCount++ > 0) {
        return;
    }
    m_resumeBinding = registered();
    unregisterKeys();
}

QString HotKeyShortcutBinder::resume()
{
    if (m_suspensionCount == 0 || --m_suspensionCount > 0) {
        return {};
    }
    QString error;
    if (m_resumeBinding) {
        m_resumeBinding = false;
        registerKeys(m_shortcut, &error);
    }
    return error;
}

// The router parks this binder while a single key holds the binding; without
// letting go of the hot key here, the replaced combination would keep firing
// alongside the key. Clearing m_resumeBinding keeps a recording's resume from
// sneaking it back.
bool HotKeyShortcutBinder::removeRegistration(QString *)
{
    m_resumeBinding = false;
    unregisterKeys();
    return true;
}

} // namespace speecher
