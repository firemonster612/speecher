#include "platform/SessionShortcutBinder.h"

#include "core/SettingsStore.h"

#include <QDebug>

#include <algorithm>

namespace speecher {
namespace {

// Why the desktop would not register a binding: another app holds it.
QString ownedElsewhereText(const ShortcutBinding &binding)
{
    return QStringLiteral("%1 is already a shortcut in another app.").arg(binding.displayText());
}

} // namespace

SessionShortcutBinder::SessionShortcutBinder(SettingsStore *settings,
                                             GlobalShortcutAction action,
                                             QObject *parent)
    : GlobalShortcutBinder(std::move(action), parent)
    , m_settings(settings)
{
}

// The router's single-key backend stores under the same key, so each side
// reads and clears only its own kind.
ShortcutBinding SessionShortcutBinder::storedShortcut() const
{
    const ShortcutBinding stored = m_settings->globalShortcut(action().role);
    return stored.isSingleKey() ? ShortcutBinding() : stored;
}

void SessionShortcutBinder::bind()
{
    m_binding = storedShortcut();
    if (!m_binding.isEmpty()) {
        m_settings->setGlobalShortcut(action().role, m_binding);
    }
    const QString error = hold();
    if (!error.isEmpty()) {
        qWarning() << "Could not take" << action().id << error;
    }
    emit bindingChanged();
}

ShortcutBinding SessionShortcutBinder::shortcut() const
{
    return m_binding;
}

bool SessionShortcutBinder::setShortcut(const ShortcutBinding &shortcut, QString *error)
{
    QString reason = unsupportedBindingReason(shortcut);
    // Taking the keys now, armed or not, is what tells another app already
    // holds them while the person is still choosing. Suspended too: a
    // recorder that binds before it resumes still learns, and the keys are
    // let go at once, so they stay free for it.
    if (reason.isEmpty() && !shortcut.isEmpty()) {
        letGo();
        m_holding = false;
        if (!take(shortcut.combination())) {
            reason = ownedElsewhereText(shortcut);
        }
        letGo();
    }
    if (!reason.isEmpty()) {
        hold();
        if (error) {
            *error = reason;
        }
        return false;
    }
    m_binding = shortcut;
    hold();
    if (!m_binding.isEmpty()) {
        m_settings->setGlobalShortcut(action().role, m_binding);
    } else {
        m_settings->forgetGlobalShortcutCombination(action().role);
    }
    emit bindingChanged();
    return true;
}

bool SessionShortcutBinder::removeRegistration(QString *)
{
    return setShortcut({});
}

void SessionShortcutBinder::setArmed(bool armed)
{
    if (armed == m_armed) {
        return;
    }
    m_armed = armed;
    const QString error = hold();
    if (!error.isEmpty()) {
        qWarning() << "Could not take" << action().id << "for this dictation:" << error;
    }
}

void SessionShortcutBinder::suspend()
{
    ++m_suspensions;
    hold();
}

QString SessionShortcutBinder::resume()
{
    m_suspensions = std::max(0, m_suspensions - 1);
    return hold();
}

void SessionShortcutBinder::holdWhileArmed(const ShortcutBinding &keys)
{
    letGo();
    m_holding = false;
    m_binding = keys;
    hold();
}

bool SessionShortcutBinder::holding() const
{
    return m_holding;
}

QString SessionShortcutBinder::hold()
{
    const bool wanted = m_armed && m_suspensions == 0 && !m_binding.isEmpty();
    if (wanted == m_holding) {
        return {};
    }
    if (!wanted) {
        letGo();
        m_holding = false;
        return {};
    }
    m_holding = take(m_binding.combination());
    return m_holding ? QString() : ownedElsewhereText(m_binding);
}

} // namespace speecher
