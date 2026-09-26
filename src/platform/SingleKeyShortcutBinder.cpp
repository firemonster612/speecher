#include "platform/SingleKeyShortcutBinder.h"

#include "core/settings/SettingsKeys.h"

#include <QSettings>

namespace speecher {
namespace {

void storeBinding(const ShortcutBinding &binding)
{
    QSettings settings(QString::fromLatin1(SettingsKeys::Organization),
                       QString::fromLatin1(SettingsKeys::Application));
    if (binding.isEmpty()) {
        // Only clear what this binder owns: on macOS the combination binder
        // stores its sequence under the same key, and a router clearing the
        // single key right after a combination was set must not erase it.
        const ShortcutBinding stored =
            ShortcutBinding::fromString(settings.value(SettingsKeys::GlobalShortcut).toString());
        if (stored.isSingleKey()) {
            settings.remove(SettingsKeys::GlobalShortcut);
        }
    } else {
        settings.setValue(SettingsKeys::GlobalShortcut, binding.toString());
    }
}

} // namespace

ShortcutBinding SingleKeyShortcutBinder::storedBinding()
{
    QSettings settings(QString::fromLatin1(SettingsKeys::Organization),
                       QString::fromLatin1(SettingsKeys::Application));
    return ShortcutBinding::fromString(settings.value(SettingsKeys::GlobalShortcut).toString());
}

QString SingleKeyShortcutBinder::unsupportedReason() const
{
    return supported() ? QString() : QStringLiteral("Single keys cannot be watched on this desktop.");
}

void SingleKeyShortcutBinder::bind()
{
    const ShortcutBinding stored = storedBinding();
    if (!stored.isSingleKey()) {
        return;
    }
    QString error;
    if (!setShortcut(stored, &error)) {
        qWarning("Could not watch the stored key %s: %s",
                 qPrintable(stored.keyCode()), qPrintable(error));
    }
}

ShortcutBinding SingleKeyShortcutBinder::shortcut() const
{
    return m_binding;
}

QString SingleKeyShortcutBinder::unsupportedBindingReason(const ShortcutBinding &binding) const
{
    if (binding.isSingleKey()) {
        return QString();
    }
    return QStringLiteral("Key combinations go through your desktop's shortcut service.");
}

bool SingleKeyShortcutBinder::setShortcut(const ShortcutBinding &shortcut, QString *error)
{
    if (shortcut.isEmpty()) {
        unwatch();
        m_down = false;
        m_binding = {};
        storeBinding(m_binding);
        emit bindingChanged();
        return true;
    }
    QString reason = unsupportedBindingReason(shortcut);
    if (reason.isEmpty()) {
        reason = watch(*physicalKey(shortcut.keyCode()));
    }
    if (!reason.isEmpty()) {
        if (error) {
            *error = reason;
        }
        return false;
    }
    m_down = false;
    m_binding = shortcut;
    storeBinding(m_binding);
    emit bindingChanged();
    return true;
}

void SingleKeyShortcutBinder::suspend()
{
    ++m_suspensionCount;
}

QString SingleKeyShortcutBinder::resume()
{
    if (m_suspensionCount == 0 || --m_suspensionCount > 0) {
        return {};
    }
    resuming();
    return {};
}

void SingleKeyShortcutBinder::keyDown(qint64 eventTimeMs)
{
    if (m_down || m_suspensionCount > 0) {
        return;
    }
    m_down = true;
    m_downEventTimeMs = eventTimeMs;
    emit activated();
}

void SingleKeyShortcutBinder::keyUp(qint64 eventTimeMs)
{
    if (!m_down) {
        return;
    }
    m_down = false;
    const qint64 heldMs = m_downEventTimeMs >= 0 && eventTimeMs >= m_downEventTimeMs
        ? eventTimeMs - m_downEventTimeMs
        : -1;
    m_downEventTimeMs = -1;
    emit deactivated(heldMs);
}

} // namespace speecher
