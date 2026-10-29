#include "platform/SingleKeyShortcutBinder.h"

#include "core/SettingsStore.h"

namespace speecher {
namespace {

// Why a session shortcut cannot be a single key that types where Speecher can
// only watch a single key, not take it (Windows, macOS): it would still type.
QString watchedKeyStillTypesText(const ShortcutBinding &binding)
{
    return QStringLiteral(
        "Speecher can only watch %1, not take it from other apps, so it would still type while "
        "you dictate. Use another key or a key combination.")
        .arg(binding.displayText());
}

} // namespace

SingleKeyShortcutBinder::SingleKeyShortcutBinder(SettingsStore *settings,
                                                 GlobalShortcutAction action,
                                                 QObject *parent)
    : GlobalShortcutBinder(std::move(action), parent)
    , m_settings(settings)
{
}

ShortcutBinding SingleKeyShortcutBinder::storedBinding() const
{
    return m_settings->globalShortcut(action().role);
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
    if (!binding.isSingleKey()) {
        return QStringLiteral("Key combinations go through your desktop's shortcut service.");
    }
    // A watcher sees a key without taking it, so a session shortcut on a key
    // that types would type into the dictation it acts on.
    if (action().sessionOnly && !singleKeyTypingWarning(binding).isEmpty()) {
        return watchedKeyStillTypesText(binding);
    }
    return QString();
}

bool SingleKeyShortcutBinder::setShortcut(const ShortcutBinding &shortcut, QString *error)
{
    if (shortcut.isEmpty()) {
        unwatch();
        m_down = false;
        m_binding = {};
        // The combination binder may store its sequence under the same key,
        // and a router clearing the single key right after a combination was
        // set must not erase it.
        m_settings->forgetGlobalShortcutSingleKey(action().role);
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
    m_settings->setGlobalShortcut(action().role, m_binding);
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
