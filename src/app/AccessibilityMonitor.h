#pragma once

#include <QObject>

class QTimer;

namespace speecher {

class PlatformComposition;

// Keeps the accessibility grant Speecher needs to read and insert into a
// Target up to date: what the platform reports, re-read when the platform says
// it changed, and on macOS, which pushes no change, polled while it is off.
class AccessibilityMonitor : public QObject {
    Q_OBJECT

public:
    explicit AccessibilityMonitor(const PlatformComposition &platform, QObject *parent = nullptr);

    bool supported() const;
    bool enabled() const;
    bool persistent() const;
    // Reads the grant once startup is out of the way, asking for it where the
    // platform keeps it across runs, and reports it whether or not it changed.
    void checkAtStartup();
    bool enablePermanently(QString *error = nullptr);
    // Reads the grant again and reports it if it changed. Platforms that do not
    // push grants to a running process poll this.
    void refresh();

signals:
    void stateChanged(bool supported, bool enabled, bool persistent);

private:
    const PlatformComposition &m_platform;
    bool m_supported = false;
    bool m_enabled = false;
    bool m_persistent = false;
#ifdef Q_OS_MACOS
    QTimer *m_poll = nullptr;
#endif
};

} // namespace speecher
