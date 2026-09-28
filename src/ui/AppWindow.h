#pragma once

#include <QHash>
#include <QMainWindow>
#include <QPoint>

class QCloseEvent;
class QFrame;
class QLabel;
class QLineEdit;
class QListWidget;
class QPaintEvent;
class QProgressBar;
class QPushButton;
class QShowEvent;
class QSplitter;
class QStackedWidget;
class QTabWidget;
class QTimer;
class QToolButton;

namespace speecher {

class InlineMessage;

class ApplicationController;
class HomePage;
class SettingsPageSet;
class TranscribePage;

class AppWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit AppWindow(ApplicationController *controller, QWidget *parent = nullptr);

    // The pane ids in the sidebar, in order, without What's New.
    QStringList sidebarPanes() const;
    // Shows a page by id: a pane id, or "pane:view" for one of its views.
    // An unknown id shows Home (see resolvePage).
    void showPage(const QString &pageId);
    // Where a window opened from hidden starts.
    void showHome();
    // Opens the Transcribe page with these files added to its list.
    void showTranscribeFiles(const QStringList &paths);
    void showWhatsNew();
    void flushPendingAutoSave();
    void rememberGeometry();

protected:
    void changeEvent(QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void buildPages();
    void buildSidebarShell();
    void rebuildSidebar();
    void selectPane(const QString &paneId);
    bool sidebarListsWhatsNew() const;
    QString currentPane() const;
    void refreshHeaderStripColor();
    void runAutoSave();

    void refreshUpdateBanner();
    void warnThatClosingDoesNotQuitOnce();
    void leaveWhatsNew();

    ApplicationController *m_controller;
    SettingsPageSet *m_pages;
    HomePage *m_home;
    TranscribePage *m_transcribe;
    // Each pane's widget in the stack, and each Alternatives pane's views by id.
    QHash<QString, QWidget *> m_paneWidgets;
    QHash<QString, QTabWidget *> m_viewTabs;
    QString m_query;
    // Whether the list was last built with What's New at its top.
    bool m_sidebarListsWhatsNew = false;
    QStackedWidget *m_stack = nullptr;
    QListWidget *m_navigation = nullptr;
    QSplitter *m_sidebarSplitter = nullptr;
    QWidget *m_sidebarPane = nullptr;
    QWidget *m_searchSection = nullptr;
    QWidget *m_headerStrip = nullptr;
    QWidget *m_headerDividerLine = nullptr;
    QWidget *m_headerUnderline = nullptr;
    QLabel *m_pageTitle = nullptr;
    QToolButton *m_backButton = nullptr;
    // The pane that was current when What's New opened.
    QString m_whatsNewReturnPane;
    InlineMessage *m_autoSaveWarning = nullptr;
    QLabel *m_autoSaveWarningText = nullptr;
    QTimer *m_autoSaveTimer = nullptr;
    InlineMessage *m_updateBanner = nullptr;
    QLabel *m_updateBannerText = nullptr;
    QProgressBar *m_updateProgress = nullptr;
    QPushButton *m_updateAction = nullptr;
    QPushButton *m_updateLater = nullptr;
    QToolButton *m_updateDismiss = nullptr;
    bool m_showingWhatsNewBanner = false;
    bool m_pageLoadScheduled = false;
    bool m_afterShowLoadScheduled = false;
    bool m_settingsDeletionStarted = false;
    bool m_headerDragPending = false;
    QPoint m_headerPressPosition;
};

} // namespace speecher
