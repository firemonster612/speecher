#pragma once

#include "frontend/quick/host/HostSingleton.h"

#include <QObject>
#include <QQmlEngine>

namespace speecher {

// The UI's Navigation singleton: which page Main shows, a row to scroll to
// and flash once that page has loaded (set by search and links to a row),
// and where a link from another page came from, for the way back. The host
// holds it, so it opens Main on a page without reaching into QML.
class QuickNavigation final : public QObject, public HostSingleton<QuickNavigation> {
    Q_OBJECT
    QML_NAMED_ELEMENT(Navigation)
    QML_SINGLETON
    // The page id Main shows; home at first.
    Q_PROPERTY(QString current READ current NOTIFY currentChanged)
    // The row id the page should land on, or empty. The page that lands on
    // it clears it.
    Q_PROPERTY(QString pendingRow READ pendingRow WRITE setPendingRow NOTIFY pendingRowChanged)
    // The page a link opened the current page from (follow), or setupOrigin
    // for the setup assistant; empty once the person goes anywhere else.
    Q_PROPERTY(QString returnTo READ returnTo NOTIFY returnToChanged)

public:
    // No default parent: the engine makes its own instance of a singleton it
    // can default-construct, and would skip create().
    explicit QuickNavigation(QObject *parent);

    // returnTo while a link from the setup assistant's window is followed.
    static inline const QString setupOrigin = QStringLiteral("setup");

    QString current() const;
    QString pendingRow() const;
    void setPendingRow(const QString &rowId);
    QString returnTo() const;

    // Shows pageId. Asking for the page already showing still says requested,
    // so the sidebar opens the group that holds it.
    Q_INVOKABLE void go(const QString &pageId);
    // Shows pageId, the page rowId is on, and lands on that row. On the page
    // already showing, pendingRowChanged fires even for the same row again.
    Q_INVOKABLE void goToRow(const QString &pageId, const QString &rowId);
    // A link from one page to another, such as Open Accounts: goToRow (or go,
    // without a row), remembering from (the current page unless given) in
    // returnTo, so the page it opens offers the way back. A link from
    // setupOrigin also asks for Main, which may not be open.
    Q_INVOKABLE void follow(const QString &pageId, const QString &rowId = {}, const QString &from = {});
    // go, or goToRow with a row, then asks for Main, which may not be open:
    // for windows with no pages of their own, such as the popup's pill.
    Q_INVOKABLE void openInMain(const QString &pageId, const QString &rowId = {});
    // Goes back where returnTo says: that page, or the setup assistant
    // (setupRequested), leaving Main on the page it shows.
    Q_INVOKABLE void goBack();
    // Forgets the way back to from, which has gone (the setup assistant
    // closed).
    void forgetReturnTo(const QString &from);

signals:
    void currentChanged();
    void pendingRowChanged();
    void returnToChanged();
    // A page was asked for, the one already showing included.
    void requested(const QString &pageId);
    // A link from the setup assistant or openInMain needs Main on screen.
    void mainRequested();
    // The way back leads to the setup assistant.
    void setupRequested();

private:
    void show(const QString &pageId);
    void setReturnTo(const QString &from);

    QString m_current = QStringLiteral("home");
    QString m_pendingRow;
    QString m_returnTo;
};

} // namespace speecher
