#include "frontend/quick/host/QuickStatus.h"

#include "app/ApplicationController.h"
#include "app/UpdateController.h"
#include "frontend/quick/host/QuickSettings.h"

namespace speecher {

namespace {

QuickStatus *g_instance = nullptr;

} // namespace

QuickStatus::QuickStatus(ApplicationController *controller, QObject *parent)
    : QObject(parent)
    , m_controller(controller)
{
    g_instance = this;
    connect(controller, &ApplicationController::globalShortcutChanged, this, &QuickStatus::shortcutKeysChanged);
    connect(controller->updates(), &UpdateController::changed, this, &QuickStatus::updateChanged);
}

QuickStatus::~QuickStatus()
{
    if (g_instance == this) {
        g_instance = nullptr;
    }
}

QuickStatus *QuickStatus::create(QQmlEngine *, QJSEngine *)
{
    Q_ASSERT_X(g_instance, "QuickStatus::create", "the host makes Status before loading QML");
    QJSEngine::setObjectOwnership(g_instance, QJSEngine::CppOwnership);
    return g_instance;
}

QStringList QuickStatus::shortcutKeys() const
{
    return speecher::shortcutKeys(m_controller->globalShortcut());
}

QString QuickStatus::version() const
{
    return m_controller->updates()->currentVersion();
}

bool QuickStatus::updateReady() const
{
    return m_controller->updates()->state() == UpdateController::State::ReadyToRestart;
}

QString QuickStatus::updateVersion() const
{
    return m_controller->updates()->availableVersionDisplay();
}

} // namespace speecher
