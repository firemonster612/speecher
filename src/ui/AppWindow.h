#pragma once

#include "ui/AppPage.h"

#include <QMainWindow>

class QActionGroup;
class QCloseEvent;
class QFrame;
class QLabel;
class QListWidget;
class QPushButton;
class QStackedWidget;
class QTimer;

namespace speecher {

class ApplicationController;
class DictationPage;
class SettingsPageSet;

class AppWindow : public QMainWindow {
    Q_OBJECT

public:
    AppWindow(ApplicationController *controller,
              const QString &prototype,
              QWidget *parent = nullptr);

    QString prototype() const;
    QStringList pageTitles() const;
    int pageCount() const;
    void navigateToSettings(AppPageId page = AppPageId::General);
    void flushPendingAutoSave();
    void rememberGeometry();

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void buildSharedPages();
    void buildSidebarShell();
    void buildToolbarShell();
    void buildCompactShell();
    QWidget *createPrototypeSwitcher(QWidget *parent);
    QWidget *createPendingBanner(QWidget *parent, bool compact = false);
    void runAutoSave();
    void updatePendingBanner();
    void showCompactPage(int page);
    void finishCompactBack();

    ApplicationController *m_controller;
    QString m_prototype;
    SettingsPageSet *m_pages;
    DictationPage *m_dictation;
    QList<QWidget *> m_pageWidgets;
    QStackedWidget *m_stack = nullptr;
    QStackedWidget *m_compactStack = nullptr;
    QStackedWidget *m_drillPages = nullptr;
    QListWidget *m_compactList = nullptr;
    QListWidget *m_navigation = nullptr;
    QActionGroup *m_navigationActions = nullptr;
    QLabel *m_drillTitle = nullptr;
    QFrame *m_pendingBanner = nullptr;
    QFrame *m_autoSaveWarning = nullptr;
    QLabel *m_autoSaveWarningText = nullptr;
    QPushButton *m_pendingApplyButton = nullptr;
    QTimer *m_autoSaveTimer = nullptr;
    bool m_pendingCompactBack = false;
};

} // namespace speecher
