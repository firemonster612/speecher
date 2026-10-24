#include "frontend/quick/host/QuickAppearance.h"

#include "app/ApplicationController.h"
#include "app/PlatformComposition.h"
#include "core/ColorSchemes.h"
#include "platform/WindowBackdrop.h"

#include <QFileInfo>
#include <QGuiApplication>
#include <QPalette>
#include <QStandardPaths>
#include <QStyleHints>
#include <QWindow>

#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
#include <QAccessibilityHints>
#endif

namespace speecher {

namespace {

QuickAppearance *g_instance = nullptr;

// Plasma keeps its current scheme's colours in kdeglobals. Off Plasma, a
// copy left by KDE apps says nothing about the desktop's colours.
QString plasmaGlobals()
{
    if (!qEnvironmentVariable("XDG_CURRENT_DESKTOP").contains(QLatin1String("KDE"))) {
        return {};
    }
    return QStandardPaths::locate(QStandardPaths::GenericConfigLocation, QStringLiteral("kdeglobals"));
}

// A scheme's roles for Theme, leaving out the ones it lacks.
QVariantMap colorsOf(const ColorScheme &scheme)
{
    QVariantMap colors;
    const auto add = [&colors](const char *role, const QColor &color) {
        if (color.isValid()) {
            colors.insert(QLatin1String(role), color);
        }
    };
    add("window", scheme.window);
    add("text", scheme.text);
    add("accent", scheme.accent);
    add("negative", scheme.negative);
    return colors;
}

bool prefersHighContrast()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    return QGuiApplication::styleHints()->accessibility()->contrastPreference() == Qt::ContrastPreference::HighContrast;
#else
    return false;
#endif
}

} // namespace

QuickAppearance::QuickAppearance(ApplicationController *controller, QObject *parent)
    : QObject(parent)
    , m_backdrop(controller->platform()->createWindowBackdrop(this))
    , m_kdeglobals(plasmaGlobals())
{
    g_instance = this;
    if (m_backdrop) {
        connect(m_backdrop, &WindowBackdrop::availableChanged, this, &QuickAppearance::canBlurChanged);
    }
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    connect(QGuiApplication::styleHints()->accessibility(), &QAccessibilityHints::contrastPreferenceChanged, this,
            &QuickAppearance::canBlurChanged);
#endif

    // KConfig saves kdeglobals by replacing it, so its folder is watched too.
    const QString globalsFolder = QFileInfo(m_kdeglobals).absolutePath();
    if (!m_kdeglobals.isEmpty()) {
        m_watcher.addPaths({m_kdeglobals, globalsFolder});
    } else {
        qGuiApp->installEventFilter(this);
    }
    for (const QString &dataDir : QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation)) {
        if (QFileInfo(colorSchemesFolder(dataDir)).isDir()) {
            m_watcher.addPath(colorSchemesFolder(dataDir));
        }
    }
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, &QuickAppearance::readSystemColors);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this, globalsFolder](const QString &folder) {
        if (!m_kdeglobals.isEmpty() && folder == globalsFolder) {
            readSystemColors();
        } else {
            readInstalledSchemes();
        }
    });
    readSystemColors();
    readInstalledSchemes();
}

QuickAppearance::~QuickAppearance()
{
    if (g_instance == this) {
        g_instance = nullptr;
    }
}

QuickAppearance *QuickAppearance::create(QQmlEngine *, QJSEngine *)
{
    Q_ASSERT_X(g_instance, "QuickAppearance::create", "the host makes Appearance before loading QML");
    QJSEngine::setObjectOwnership(g_instance, QJSEngine::CppOwnership);
    return g_instance;
}

bool QuickAppearance::canBlur() const
{
    return m_backdrop && m_backdrop->available() && !prefersHighContrast();
}

void QuickAppearance::setBlurBehind(QWindow *window, const QRectF &area, int radius, bool dark)
{
    if (m_backdrop) {
        m_backdrop->set(window, area.toAlignedRect(), radius, dark);
    }
}

bool QuickAppearance::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == qGuiApp && event->type() == QEvent::ApplicationPaletteChange) {
        readSystemColors();
    }
    return false;
}

void QuickAppearance::readSystemColors()
{
    QVariantMap colors;
    if (!m_kdeglobals.isEmpty()) {
        const ColorScheme scheme = readKdeColorScheme(m_kdeglobals);
        if (scheme.isValid()) {
            colors = colorsOf(scheme);
        }
        // Replacing the file drops it from the watcher.
        if (!m_watcher.files().contains(m_kdeglobals) && QFileInfo::exists(m_kdeglobals)) {
            m_watcher.addPath(m_kdeglobals);
        }
    }
    // Off Plasma, or with Plasma's defaults, which kdeglobals leaves out, the
    // platform's accent is what the desktop says.
    if (colors.isEmpty()) {
        colors.insert(QStringLiteral("accent"), QGuiApplication::palette().color(QPalette::Accent));
    }
    if (colors == m_systemColors) {
        return;
    }
    m_systemColors = colors;
    emit systemColorsChanged();
}

void QuickAppearance::readInstalledSchemes()
{
    QVariantList schemes;
    for (const ColorScheme &scheme :
         installedColorSchemes(QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation))) {
        schemes.append(QVariantMap{
            {QStringLiteral("id"), scheme.id},
            {QStringLiteral("name"), scheme.name},
            {QStringLiteral("colors"), colorsOf(scheme)},
        });
    }
    if (schemes == m_installedSchemes) {
        return;
    }
    m_installedSchemes = schemes;
    emit installedSchemesChanged();
}

} // namespace speecher
