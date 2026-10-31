#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QTimer>

#include <functional>
#include <optional>

namespace speecher {

struct KWinWindowInfo {
    QString resourceClass;
    QString resourceName;
    QString caption;
    // KWin's own handle for this window (internalId), stable for the window's
    // whole life and unique across windows of the same program. Empty when the
    // compositor does not expose it, which is what the caller falls back from.
    QString internalId;
    qint64 processId = 0;
};

// Asks KWin which window is active. Accessibility is the primary way Speecher
// identifies the focus target, but GPU terminals (Ghostty, Kitty, Alacritty)
// often never reach the AT-SPI bus, so a dictation into them would otherwise
// fall back to the global paste chord. On KDE the compositor still knows the
// window, so this fills the gap when the a11y tree is silent.
//
// Off KDE, or when KWin does not answer in time, activeWindow() answers
// nothing and the caller keeps its previous behaviour.
class KWinActiveWindow : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.speecher.WindowProbe")

public:
    explicit KWinActiveWindow(QObject *parent = nullptr);
    ~KWinActiveWindow() override;

    // Asks KWin and returns at once. done hears the active window, or nothing
    // off KDE, for Speecher's own surface, when KWin turns the script down or
    // takes over half a second to load it, or when it does not report within
    // timeoutMs of running it. Asked again before then, the probe under way
    // answers both.
    void activeWindow(std::function<void(const std::optional<KWinWindowInfo> &)> done, int timeoutMs = 250);

public slots:
    // D-Bus entry point the loaded KWin script calls back into. Exported over
    // the session bus as a scriptable slot; not meant to be called directly.
    Q_SCRIPTABLE void reportWindow(const QString &resourceClass,
                                   const QString &resourceName,
                                   const QString &caption,
                                   const QString &processId,
                                   const QString &internalId,
                                   const QString &probe);

private:
    bool ensureRegistered();
    // Ends the probe under way, answering everyone waiting for it.
    void answer(const std::optional<KWinWindowInfo> &window);

    QString m_objectPath;
    bool m_registered = false;
    QTimer m_timeout;
    // Counts probes, so a late reply from one that timed out is not taken
    // for the next one's.
    quint64 m_probes = 0;
    QList<std::function<void(const std::optional<KWinWindowInfo> &)>> m_waiting;
};

} // namespace speecher
