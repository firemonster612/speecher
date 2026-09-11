#pragma once

#include <QObject>
#include <QString>

#include <optional>

namespace speecher {

struct KWinWindowInfo {
    QString resourceClass;
    QString resourceName;
    QString caption;
    qint64 processId = 0;
};

// Asks KWin which window is active. Accessibility is the primary way Speecher
// identifies the focus target, but GPU terminals (Ghostty, Kitty, Alacritty)
// often never reach the AT-SPI bus, so a dictation into them would otherwise
// fall back to the global paste chord. On KDE the compositor still knows the
// window, so this fills the gap when the a11y tree is silent.
//
// Off KDE, or when KWin does not answer in time, activeWindow() returns nullopt
// and the caller keeps its previous behaviour.
class KWinActiveWindow : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.speecher.WindowProbe")

public:
    explicit KWinActiveWindow(QObject *parent = nullptr);
    ~KWinActiveWindow() override;

    std::optional<KWinWindowInfo> activeWindow(int timeoutMs = 250);

public slots:
    // D-Bus entry point the loaded KWin script calls back into. Exported over
    // the session bus as a scriptable slot; not meant to be called directly.
    Q_SCRIPTABLE void reportWindow(const QString &resourceClass,
                                   const QString &resourceName,
                                   const QString &caption,
                                   const QString &processId);

private:
    bool ensureRegistered();

    QString m_objectPath;
    bool m_registered = false;
    bool m_reported = false;
    KWinWindowInfo m_pending;
};

} // namespace speecher
