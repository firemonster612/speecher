#pragma once

#include <QObject>
#include <QString>

namespace speecher {

class SettingsStore;

// Records the Last-run Version at each launch and, after an upgrade, holds the
// version the What's New Page starts from until the person has seen it.
class WhatsNewTracker : public QObject {
    Q_OBJECT

public:
    // Compares this build with the Last-run Version in settings and records
    // this one as the new Last-run Version.
    explicit WhatsNewTracker(SettingsStore &settings, QObject *parent = nullptr);

    // The Last-run Version the What's New Page covers releases after, or
    // empty when there is nothing new to show.
    QString pendingVersion() const;
    // The What's New Page was shown; the next launch shows nothing new.
    void clearPending();

signals:
    void pendingChanged();

private:
    SettingsStore &m_settings;
    QString m_pendingVersion;
};

} // namespace speecher
