#include "frontend/quick/host/QuickAppearance.h"

#include "app/ApplicationController.h"
#include "app/PlatformComposition.h"
#include "core/ColorSchemes.h"
#include "platform/DesktopColors.h"
#include "platform/WindowBackdrop.h"

#include <QFileInfo>
#include <QGuiApplication>
#include <QStandardPaths>
#include <QStyleHints>
#include <QWindow>

#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
#include <QAccessibilityHints>
#endif

namespace speecher {

namespace {

QuickAppearance *g_instance = nullptr;

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
    , m_desktopColors(controller->platform()->createDesktopColors(this))
{
    g_instance = this;
    if (m_backdrop) {
        connect(m_backdrop, &WindowBackdrop::availableChanged, this, &QuickAppearance::canBlurChanged);
    }
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    connect(QGuiApplication::styleHints()->accessibility(), &QAccessibilityHints::contrastPreferenceChanged, this,
            &QuickAppearance::canBlurChanged);
#endif
    if (m_desktopColors) {
        connect(m_desktopColors, &DesktopColors::changed, this, &QuickAppearance::readSystemColors);
    }

    for (const QString &dataDir : QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation)) {
        if (QFileInfo(colorSchemesFolder(dataDir)).isDir()) {
            m_schemeFolders.addPath(colorSchemesFolder(dataDir));
        }
    }
    connect(&m_schemeFolders, &QFileSystemWatcher::directoryChanged, this, &QuickAppearance::readInstalledSchemes);
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

void QuickAppearance::readSystemColors()
{
    if (!m_desktopColors) {
        return;
    }
    // The desktop's defaults are no colours here: the UI takes the
    // platform's accent then.
    const ColorScheme scheme = m_desktopColors->scheme();
    const QVariantMap colors = scheme.isValid() ? colorsOf(scheme) : QVariantMap{};
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
