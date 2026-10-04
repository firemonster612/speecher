#include "platform/KGlobalAccelShortcutBinder.h"

#include "core/settings/SettingsSchema.h"
#include "platform/StoredCombination.h"

#include <QAction>
#include <QDebug>

#ifdef SPEECHER_WITH_KGLOBALACCEL
#include <KGlobalAccel>
#endif

namespace speecher {
namespace {

constexpr auto shortcutComponent = "io.github.firemonster612.speecher";
constexpr auto legacyShortcutComponent = "local.speecher";

// KGlobalAccel's key list for one sequence; an unbound shortcut has none.
QList<QKeySequence> keyList(const QKeySequence &sequence)
{
    return sequence.isEmpty() ? QList<QKeySequence>{} : QList<QKeySequence>{sequence};
}

} // namespace

KGlobalAccelShortcutBinder::KGlobalAccelShortcutBinder(GlobalShortcutAction action, QObject *parent)
    : GlobalShortcutBinder(std::move(action), parent)
{
#ifdef SPEECHER_WITH_KGLOBALACCEL
    // Press arrives through QAction::triggered; the daemon reports release
    // only through this signal, with no trigger of its own.
    connect(KGlobalAccel::self(), &KGlobalAccel::globalShortcutActiveChanged,
            this, [this](QAction *action, bool active) {
                if (action == m_action && !active) {
                    emit deactivated();
                }
            });
#endif
}

bool KGlobalAccelShortcutBinder::supported() const
{
#ifdef SPEECHER_WITH_KGLOBALACCEL
    return qEnvironmentVariable("XDG_CURRENT_DESKTOP").contains(
        QStringLiteral("KDE"),
        Qt::CaseInsensitive);
#else
    return false;
#endif
}

QString KGlobalAccelShortcutBinder::unsupportedReason() const
{
    return supported() ? QString() : QStringLiteral("KGlobalAccel is unavailable");
}

// The action KGlobalAccel identifies Speecher's shortcut by. Built on demand so
// removal works in a process that never bound.
QAction *KGlobalAccelShortcutBinder::makeShortcutAction()
{
    delete m_action;
    m_action = new QAction(action().description, this);
    // Holding the keys must not re-trigger: kglobalaccel drops its Repeated
    // states when the action opts out of auto-repeat.
    m_action->setAutoRepeat(false);
    m_action->setObjectName(action().id);
    m_action->setProperty("componentName", QString::fromLatin1(shortcutComponent));
    m_action->setProperty("componentDisplayName", QStringLiteral("Speecher"));
    connect(m_action, &QAction::triggered, this, &GlobalShortcutBinder::activated);
    return m_action;
}

void KGlobalAccelShortcutBinder::bind()
{
#ifdef SPEECHER_WITH_KGLOBALACCEL
    if (action().duringDictationOnly) {
        m_binding = storedCombination(action().settingsKey);
        // Nothing stays registered between sessions, including what an older
        // build left with the daemon for good.
        releaseFromDaemon();
        const QString error = holdWhileArmed();
        if (!error.isEmpty()) {
            qWarning() << "Could not register" << action().id << error;
        }
        emit bindingChanged();
        return;
    }
    QKeySequence savedShortcut = shortcut().combination();
    const QList<QKeySequence> legacyShortcuts = KGlobalAccel::self()->globalShortcut(
        QString::fromLatin1(legacyShortcutComponent),
        action().id);
    if (savedShortcut.isEmpty() && !legacyShortcuts.isEmpty()) {
        savedShortcut = legacyShortcuts.first();
    }
    KGlobalAccel::self()->cleanComponent(QString::fromLatin1(legacyShortcutComponent));
    makeShortcutAction();
    const QKeySequence defaultShortcut = action().defaultShortcut;
    if (!KGlobalAccel::self()->setDefaultShortcut(m_action, keyList(defaultShortcut))) {
        qWarning() << "Could not set the default Global Shortcut"
                   << QString::fromLatin1(shortcutComponent) << defaultShortcut;
    }
    const QKeySequence wanted = savedShortcut.isEmpty() ? defaultShortcut : savedShortcut;
    if (!KGlobalAccel::self()->setShortcut(m_action, keyList(wanted), KGlobalAccel::Autoloading)) {
        qWarning() << "Could not activate the Global Shortcut" << wanted;
    }
    // The daemon reports success but stores no key when another component owns it.
    const QList<QKeySequence> active = KGlobalAccel::self()->shortcut(m_action);
    if (!wanted.isEmpty() && (active.isEmpty() || active.first() != wanted)) {
        qWarning() << "Global Shortcut differs from the requested keys: wanted" << wanted
                   << "active" << (active.isEmpty() ? QKeySequence() : active.first());
    }
    emit bindingChanged();
#endif
}

ShortcutBinding KGlobalAccelShortcutBinder::shortcut() const
{
#ifdef SPEECHER_WITH_KGLOBALACCEL
    if (action().duringDictationOnly) {
        return m_binding;
    }
    const QList<QKeySequence> shortcuts = KGlobalAccel::self()->globalShortcut(
        QString::fromLatin1(shortcutComponent),
        action().id);
    return shortcuts.isEmpty() ? ShortcutBinding() : ShortcutBinding(shortcuts.first());
#else
    return {};
#endif
}

bool KGlobalAccelShortcutBinder::setShortcut(const ShortcutBinding &shortcut, QString *error)
{
#ifdef SPEECHER_WITH_KGLOBALACCEL
    const QString reason = unsupportedBindingReason(shortcut);
    if (!reason.isEmpty()) {
        if (error) {
            *error = reason;
        }
        return false;
    }
    if (action().duringDictationOnly) {
        // Registering now, armed or not, is what tells another component
        // already owns the keys while the person is still choosing them.
        const ShortcutBinding previous = m_binding;
        m_binding = shortcut;
        if (m_action) {
            releaseFromDaemon();
        }
        QString refusal;
        if (!shortcut.isEmpty() && !m_suspended
            && !registerWithDaemon(shortcut.combination(), &refusal)) {
            m_binding = previous;
            holdWhileArmed();
            if (error) {
                *error = refusal;
            }
            return false;
        }
        holdWhileArmed();
        storeCombination(action().settingsKey, m_binding);
        emit bindingChanged();
        return true;
    }
    // removeRegistration() deletes the action when a single key takes over;
    // choosing a combination again arrives here without a bind() in between,
    // so recreate the action (and its activation connection) on demand.
    if (!m_action) {
        makeShortcutAction();
    }
    if (!KGlobalAccel::self()->setShortcut(
            m_action, keyList(shortcut.combination()), KGlobalAccel::NoAutoloading)) {
        if (error) {
            *error = QStringLiteral("The desktop global-shortcut service rejected the key sequence");
        }
        return false;
    }
    emit bindingChanged();
    return true;
#else
    Q_UNUSED(shortcut)
    if (error) {
        *error = unsupportedReason();
    }
    return false;
#endif
}

bool KGlobalAccelShortcutBinder::removeRegistration(QString *error)
{
#ifdef SPEECHER_WITH_KGLOBALACCEL
    if (!supported()) {
        if (error) {
            *error = unsupportedReason();
        }
        return false;
    }
    if (this->action().duringDictationOnly) {
        releaseFromDaemon();
        m_binding = {};
        storeCombination(this->action().settingsKey, m_binding);
        emit bindingChanged();
        return true;
    }
    QAction *action = m_action ? m_action : makeShortcutAction();
    KGlobalAccel::self()->removeAllShortcuts(action);
    KGlobalAccel::self()->cleanComponent(QString::fromLatin1(shortcutComponent));
    delete m_action;
    m_action = nullptr;
    emit bindingChanged();
    return true;
#else
    if (error) {
        *error = unsupportedReason();
    }
    return false;
#endif
}

void KGlobalAccelShortcutBinder::setArmed(bool armed)
{
    if (!action().duringDictationOnly || armed == m_armed) {
        return;
    }
    m_armed = armed;
    const QString error = holdWhileArmed();
    if (!error.isEmpty()) {
        qWarning() << "Could not register" << action().id << "for this dictation:" << error;
    }
}

void KGlobalAccelShortcutBinder::suspend()
{
    if (action().duringDictationOnly) {
        m_suspended = true;
        holdWhileArmed();
    }
}

QString KGlobalAccelShortcutBinder::resume()
{
    if (!action().duringDictationOnly) {
        return {};
    }
    m_suspended = false;
    return holdWhileArmed();
}

QString KGlobalAccelShortcutBinder::holdWhileArmed()
{
    if (!m_armed || m_suspended || m_binding.isEmpty()) {
        if (m_action) {
            releaseFromDaemon();
        }
        return {};
    }
    if (m_action) {
        return {};
    }
    QString error;
    registerWithDaemon(m_binding.combination(), &error);
    return error;
}

bool KGlobalAccelShortcutBinder::registerWithDaemon(const QKeySequence &keys, QString *error)
{
#ifdef SPEECHER_WITH_KGLOBALACCEL
    makeShortcutAction();
    // The daemon reports success but stores no key when another component
    // owns it, so the keys it kept are the answer.
    KGlobalAccel::self()->setShortcut(m_action, keyList(keys), KGlobalAccel::NoAutoloading);
    const QList<QKeySequence> active = KGlobalAccel::self()->shortcut(m_action);
    if (!active.isEmpty() && active.first() == keys) {
        return true;
    }
    releaseFromDaemon();
    if (error) {
        *error = globalShortcutOwnedElsewhereText(ShortcutBinding(keys));
    }
    return false;
#else
    Q_UNUSED(keys)
    if (error) {
        *error = unsupportedReason();
    }
    return false;
#endif
}

void KGlobalAccelShortcutBinder::releaseFromDaemon()
{
#ifdef SPEECHER_WITH_KGLOBALACCEL
    QAction *action = m_action ? m_action : makeShortcutAction();
    KGlobalAccel::self()->removeAllShortcuts(action);
    delete m_action;
    m_action = nullptr;
#endif
}

} // namespace speecher
