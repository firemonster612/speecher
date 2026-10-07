#include "platform/KGlobalAccelShortcutBinder.h"

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

bool kglobalaccelAvailable()
{
#ifdef SPEECHER_WITH_KGLOBALACCEL
    return qEnvironmentVariable("XDG_CURRENT_DESKTOP").contains(
        QStringLiteral("KDE"),
        Qt::CaseInsensitive);
#else
    return false;
#endif
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
    return kglobalaccelAvailable();
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

KGlobalAccelSessionShortcutBinder::KGlobalAccelSessionShortcutBinder(GlobalShortcutAction action,
                                                                     QObject *parent)
    : SessionShortcutBinder(std::move(action), parent)
{
}

bool KGlobalAccelSessionShortcutBinder::supported() const
{
    return kglobalaccelAvailable();
}

QString KGlobalAccelSessionShortcutBinder::unsupportedReason() const
{
    return supported() ? QString() : QStringLiteral("KGlobalAccel is unavailable");
}

// Builds before session shortcuts kept the Cancel Shortcut with the daemon
// for good. Carry it over, then let go of it: nothing stays registered
// between sessions.
ShortcutBinding KGlobalAccelSessionShortcutBinder::storedShortcut() const
{
    const ShortcutBinding stored = SessionShortcutBinder::storedShortcut();
#ifdef SPEECHER_WITH_KGLOBALACCEL
    if (stored.isEmpty()) {
        const QList<QKeySequence> registered = KGlobalAccel::self()->globalShortcut(
            QString::fromLatin1(shortcutComponent), action().id);
        if (!registered.isEmpty()) {
            return ShortcutBinding(registered.first());
        }
    }
#endif
    return stored;
}

void KGlobalAccelSessionShortcutBinder::bind()
{
    SessionShortcutBinder::bind();
    dropLeftoverRegistration();
}

void KGlobalAccelSessionShortcutBinder::dropLeftoverRegistration()
{
    if (!m_action) {
        take({});
        letGo();
    }
}

bool KGlobalAccelSessionShortcutBinder::take(const QKeySequence &keys)
{
#ifdef SPEECHER_WITH_KGLOBALACCEL
    delete m_action;
    m_action = new QAction(action().description, this);
    m_action->setAutoRepeat(false);
    m_action->setObjectName(action().id);
    m_action->setProperty("componentName", QString::fromLatin1(shortcutComponent));
    m_action->setProperty("componentDisplayName", QStringLiteral("Speecher"));
    connect(m_action, &QAction::triggered, this, &GlobalShortcutBinder::activated);
    // The daemon reports success but stores no key when another component
    // owns it, so the keys it kept are the answer.
    KGlobalAccel::self()->setShortcut(m_action, keyList(keys), KGlobalAccel::NoAutoloading);
    const QList<QKeySequence> active = KGlobalAccel::self()->shortcut(m_action);
    return !active.isEmpty() && active.first() == keys;
#else
    Q_UNUSED(keys)
    return false;
#endif
}

void KGlobalAccelSessionShortcutBinder::letGo()
{
#ifdef SPEECHER_WITH_KGLOBALACCEL
    if (m_action) {
        KGlobalAccel::self()->removeAllShortcuts(m_action);
        delete m_action;
        m_action = nullptr;
    }
#endif
}

} // namespace speecher
