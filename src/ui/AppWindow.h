#pragma once

#include "ui/AppPage.h"

#include <QMainWindow>
#include <QPoint>

class QCloseEvent;
class QFrame;
class QLabel;
class QLineEdit;
class QListWidget;
class QListView;
class QPaintEvent;
class QProgressBar;
class QPushButton;
class QShowEvent;
class QSplitter;
class QStackedWidget;
class QTimer;
class QToolButton;
class QVBoxLayout;

#ifdef SPEECHER_WITH_KPAGEWIDGET
class KPageWidget;
class KPageWidgetItem;
#endif

namespace speecher {

class ApplicationController;
class DictationPage;
class SettingsPageSet;

class AppWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit AppWindow(ApplicationController *controller, QWidget *parent = nullptr);

    QStringList pageTitles() const;
    int pageCount() const;
    void navigateToSettings(AppPageId page = AppPageId::General);
    void flushPendingAutoSave();
    void rememberGeometry();

protected:
    void changeEvent(QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void buildSharedPages();
    void buildSidebarShell();
    void buildStatusBanners(QWidget *parent, QVBoxLayout *layout);
    void watchHeaderColorConfig();
#ifdef SPEECHER_WITH_KPAGEWIDGET
    void buildNativeSidebarShell();
#endif
    void refreshHeaderStripColor();
    void runAutoSave();
    void filterSidebarPages(const QString &query);
    void refreshUpdateBanner();
    void showWhatsNew();
    void leaveWhatsNew();

    ApplicationController *m_controller;
    SettingsPageSet *m_pages;
    DictationPage *m_dictation;
    QList<QWidget *> m_pageWidgets;
#ifdef SPEECHER_WITH_KPAGEWIDGET
    KPageWidget *m_navigation = nullptr;
    QList<KPageWidgetItem *> m_navigationPages;
    QListView *m_navigationView = nullptr;
#else
    QStackedWidget *m_stack = nullptr;
    QListWidget *m_navigation = nullptr;
#endif
    QSplitter *m_sidebarSplitter = nullptr;
    QWidget *m_sidebarPane = nullptr;
    QWidget *m_searchSection = nullptr;
    QWidget *m_headerStrip = nullptr;
    QWidget *m_headerDividerLine = nullptr;
    QWidget *m_headerUnderline = nullptr;
    QStringList m_pageKeywords;
    QLabel *m_pageTitle = nullptr;
    QToolButton *m_backButton = nullptr;
    // The sidebar row that was current when What's New opened.
    int m_whatsNewReturnRow = 0;
    QFrame *m_autoSaveWarning = nullptr;
    QLabel *m_autoSaveWarningText = nullptr;
    QTimer *m_autoSaveTimer = nullptr;
    QFrame *m_updateBanner = nullptr;
    QLabel *m_updateBannerText = nullptr;
    QProgressBar *m_updateProgress = nullptr;
    QPushButton *m_updateAction = nullptr;
    QPushButton *m_updateLater = nullptr;
    QToolButton *m_updateDismiss = nullptr;
    bool m_updateBannerDeferred = false;
    QString m_updateBannerVersion;
    QString m_updateBannerInstalledVersion;
    bool m_showingWhatsNewBanner = false;
    bool m_pageLoadScheduled = false;
    bool m_afterShowLoadScheduled = false;
    bool m_settingsDeletionStarted = false;
    bool m_headerDragPending = false;
    QPoint m_headerPressPosition;
};

} // namespace speecher
