#include "platform/SessionShortcutBinder.h"

#include "core/settings/SettingsKeys.h"
#include "core/settings/SettingsSchema.h"

#include <QDebug>
#include <QSettings>

#include <algorithm>

namespace speecher {
namespace {

QSettings speecherSettings()
{
    return QSettings(QString::fromLatin1(SettingsKeys::Organization),
                     QString::fromLatin1(SettingsKeys::Application));
}

} // namespace

// The router's single-key backend stores under the same key, so each side
// reads and clears only its own kind.
ShortcutBinding SessionShortcutBinder::storedShortcut() const
{
    const ShortcutBinding stored =
        ShortcutBinding::fromString(speecherSettings().value(action().settingsKey).toString());
    return stored.isSingleKey() ? ShortcutBinding() : stored;
}

void SessionShortcutBinder::bind()
{
    m_binding = storedShortcut();
    if (!m_binding.isEmpty()) {
        speecherSettings().setValue(action().settingsKey, m_binding.toString());
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
            reason = globalShortcutOwnedElsewhereText(shortcut);
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
    QSettings settings = speecherSettings();
    if (!m_binding.isEmpty()) {
        settings.setValue(action().settingsKey, m_binding.toString());
    } else if (!ShortcutBinding::fromString(settings.value(action().settingsKey).toString()).isSingleKey()) {
        settings.remove(action().settingsKey);
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
    return m_holding ? QString() : globalShortcutOwnedElsewhereText(m_binding);
}

} // namespace speecher
