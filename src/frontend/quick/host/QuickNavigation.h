#pragma once

#include "frontend/quick/host/HostSingleton.h"

#include <QObject>
#include <QQmlEngine>

namespace speecher {

// The UI's Navigation singleton: which page Main shows, and a row to scroll
// to and flash once that page has loaded (set by search and links to a row).
// The host holds it, so it opens Main on a page without reaching into QML.
class QuickNavigation final : public QObject, public HostSingleton<QuickNavigation> {
    Q_OBJECT
    QML_NAMED_ELEMENT(Navigation)
    QML_SINGLETON
    // The page id Main shows; home at first.
    Q_PROPERTY(QString current READ current NOTIFY currentChanged)
    // The row id the page should land on, or empty. The page that lands on
    // it clears it.
    Q_PROPERTY(QString pendingRow READ pendingRow WRITE setPendingRow NOTIFY pendingRowChanged)

public:
    // No default parent: the engine makes its own instance of a singleton it
    // can default-construct, and would skip create().
    explicit QuickNavigation(QObject *parent);

    QString current() const;
    QString pendingRow() const;
    void setPendingRow(const QString &rowId);

    // Shows pageId. Asking for the page already showing still says requested,
    // so the sidebar opens the group that holds it.
    Q_INVOKABLE void go(const QString &pageId);
    // Shows pageId, the page rowId is on, and lands on that row. On the page
    // already showing, pendingRowChanged fires even for the same row again.
    Q_INVOKABLE void goToRow(const QString &pageId, const QString &rowId);

signals:
    void currentChanged();
    void pendingRowChanged();
    // A page was asked for, the one already showing included.
    void requested(const QString &pageId);

private:
    void show(const QString &pageId);

    QString m_current = QStringLiteral("home");
    QString m_pendingRow;
};

} // namespace speecher
