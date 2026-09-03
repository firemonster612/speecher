#include "platform/KGlobalAccelShortcutBinder.h"

#include <QAction>
#include <QDebug>

#ifdef SPEECHER_WITH_KGLOBALACCEL
#include <KGlobalAccel>
#endif

namespace speecher {
namespace {

constexpr auto shortcutComponent = "io.github.firemonster612.speecher";
constexpr auto shortcutAction = "toggle-dictation";

} // namespace

KGlobalAccelShortcutBinder::KGlobalAccelShortcutBinder(QObject *parent)
    : GlobalShortcutBinder(parent)
{
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

void KGlobalAccelShortcutBinder::bind()
{
#ifdef SPEECHER_WITH_KGLOBALACCEL
    KGlobalAccel::self()->cleanComponent(QStringLiteral("local.speecher"));
    const QKeySequence savedShortcut = shortcut();
    delete m_action;
    m_action = new QAction(QStringLiteral("Toggle dictation"), this);
    m_action->setObjectName(QString::fromLatin1(shortcutAction));
    m_action->setProperty("componentName", QString::fromLatin1(shortcutComponent));
    m_action->setProperty("componentDisplayName", QStringLiteral("Speecher"));
    connect(m_action, &QAction::triggered, this, &GlobalShortcutBinder::activated);
    const QKeySequence defaultShortcut = GlobalShortcutBinder::defaultShortcut();
    if (!KGlobalAccel::self()->setDefaultShortcut(m_action, {defaultShortcut})) {
        qWarning() << "Could not set the default Global Shortcut"
                   << QString::fromLatin1(shortcutComponent) << defaultShortcut;
    }
    if (!savedShortcut.isEmpty()
        && !KGlobalAccel::self()->setShortcut(
            m_action,
            {savedShortcut},
            KGlobalAccel::Autoloading)) {
        qWarning() << "Could not restore the saved Global Shortcut";
    }
    emit bindingChanged();
#endif
}

QKeySequence KGlobalAccelShortcutBinder::shortcut() const
{
#ifdef SPEECHER_WITH_KGLOBALACCEL
    const QList<QKeySequence> shortcuts = KGlobalAccel::self()->globalShortcut(
        QString::fromLatin1(shortcutComponent),
        QString::fromLatin1(shortcutAction));
    return shortcuts.isEmpty() ? QKeySequence() : shortcuts.first();
#else
    return {};
#endif
}

bool KGlobalAccelShortcutBinder::setShortcut(const QKeySequence &shortcut, QString *error)
{
#ifdef SPEECHER_WITH_KGLOBALACCEL
    if (shortcut.isEmpty()) {
        if (error) {
            *error = QStringLiteral("Choose a key sequence");
        }
        return false;
    }
    if (!KGlobalAccel::self()->setShortcut(
            m_action,
            {shortcut},
            KGlobalAccel::NoAutoloading)) {
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

} // namespace speecher
