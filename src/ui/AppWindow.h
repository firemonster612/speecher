#pragma once

#include "ui/AppPage.h"

#include <QMainWindow>
#include <QPoint>

class QCloseEvent;
class QFrame;
class QLabel;
class QLineEdit;
class QListWidget;
class QPaintEvent;
class QShowEvent;
class QSplitter;
class QStackedWidget;
class QTimer;

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
    void refreshHeaderStripColor();
    void runAutoSave();
    void filterSidebarPages(const QString &query);

    ApplicationController *m_controller;
    SettingsPageSet *m_pages;
    DictationPage *m_dictation;
    QList<QWidget *> m_pageWidgets;
    QStackedWidget *m_stack = nullptr;
    QListWidget *m_navigation = nullptr;
    QSplitter *m_sidebarSplitter = nullptr;
    QWidget *m_sidebarPane = nullptr;
    QWidget *m_searchSection = nullptr;
    QWidget *m_headerStrip = nullptr;
    QWidget *m_headerDividerLine = nullptr;
    QWidget *m_headerUnderline = nullptr;
    QStringList m_pageKeywords;
    QLabel *m_pageTitle = nullptr;
    QFrame *m_autoSaveWarning = nullptr;
    QLabel *m_autoSaveWarningText = nullptr;
    QTimer *m_autoSaveTimer = nullptr;
    bool m_pageLoadScheduled = false;
    bool m_afterShowLoadScheduled = false;
    bool m_headerDragPending = false;
    QPoint m_headerPressPosition;
};

} // namespace speecher
