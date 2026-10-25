#pragma once

#include <QDateTime>
#include <QFileSystemWatcher>
#include <QObject>
#include <QQmlEngine>
#include <QRectF>
#include <QVariantList>
#include <QVariantMap>
#include <QWindow>

namespace speecher {

class ApplicationController;
class WindowBackdrop;

// The UI's Appearance singleton: what the desktop offers the window's look.
// Colours are a scheme's roles, { window, text, accent, negative } (any may
// be missing), which Theme maps onto its palette.
class QuickAppearance final : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(Appearance)
    QML_SINGLETON
    // The desktop's current colours: on Plasma, its colour scheme as
    // kdeglobals holds it, followed as it changes; elsewhere none, and the
    // UI takes the platform palette's accent.
    Q_PROPERTY(QVariantMap systemColors READ systemColors NOTIFY systemColorsChanged)
    // The desktop's installed colour schemes, { id, name, colors }, by name.
    Q_PROPERTY(QVariantList installedSchemes READ installedSchemes NOTIFY installedSchemesChanged)
    // Whether the compositor blurs behind a window now, and the person has
    // not asked for less transparency or more contrast.
    Q_PROPERTY(bool canBlur READ canBlur NOTIFY canBlurChanged)

public:
    explicit QuickAppearance(ApplicationController *controller, QObject *parent = nullptr);
    ~QuickAppearance() override;

    // The engine's instance: the one the host made before loading any QML.
    static QuickAppearance *create(QQmlEngine *, QJSEngine *);

    QVariantMap systemColors() const { return m_systemColors; }
    QVariantList installedSchemes() const { return m_installedSchemes; }
    bool canBlur() const;

    // Blurs the desktop behind area of window, a rectangle with rounded
    // corners in the window's coordinates; an empty area stops. dark asks
    // for the platform's dark material where it has one.
    Q_INVOKABLE void setBlurBehind(QWindow *window, const QRectF &area, int radius, bool dark);

signals:
    void systemColorsChanged();
    void installedSchemesChanged();
    void canBlurChanged();

private:
    void readSystemColors();
    void readInstalledSchemes();

    WindowBackdrop *m_backdrop;
    const bool m_onPlasma;
    QFileSystemWatcher m_watcher;
    QStringList m_configFolders;
    // The kdeglobals in effect, and when it was last changed, as last read.
    QString m_kdeglobals;
    QDateTime m_kdeglobalsModified;
    QVariantMap m_systemColors;
    QVariantList m_installedSchemes;
};

} // namespace speecher
