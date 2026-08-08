#include "ui/AppWindow.h"

#include "app/ApplicationController.h"
#include "core/SettingsStore.h"
#include "ui/DictationPage.h"
#include "ui/settings/BindingsSettingsPage.h"
#include "ui/settings/ApplicationSettingsPage.h"
#include "ui/settings/AudioSettingsPage.h"
#include "ui/settings/CorrectionsSettingsPage.h"
#include "ui/settings/GeneralSettingsPage.h"
#include "ui/settings/OutputSettingsPage.h"
#include "ui/settings/ProviderSettingsPage.h"
#include "ui/settings/RefinementSettingsPage.h"
#include "ui/settings/SettingsPageSet.h"
#include "ui/settings/SettingsPageSupport.h"
#include "ui/settings/VocabularySettingsPage.h"

#include <QActionGroup>
#include <QCloseEvent>
#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

#include <utility>

namespace speecher {

namespace {

const QList<QPair<QString, QString>> kPages{
    {QStringLiteral("Dictation"), QStringLiteral("audio-input-microphone")},
    {QStringLiteral("General"), QStringLiteral("preferences-system")},
    {QStringLiteral("Audio"), QStringLiteral("audio-card")},
    {QStringLiteral("Applications"), QStringLiteral("applications-system")},
    {QStringLiteral("Output"), QStringLiteral("edit-paste")},
    {QStringLiteral("Refinement"), QStringLiteral("document-edit")},
    {QStringLiteral("Vocabulary"), QStringLiteral("tools-check-spelling")},
};

QScrollArea *scrollingPage(QWidget *content, QWidget *parent)
{
    auto *scroll = new QScrollArea(parent);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(content);
    return scroll;
}

QWidget *detachedContent(QScrollArea *page)
{
    QWidget *content = page->takeWidget();
    page->hide();
    return content;
}

} // namespace

AppWindow::AppWindow(ApplicationController *controller,
                     const QString &prototype,
                     QWidget *parent)
    : QMainWindow(parent)
    , m_controller(controller)
    , m_prototype(prototype)
    , m_pages(new SettingsPageSet(controller, this))
    , m_dictation(new DictationPage(controller, this))
{
    setObjectName(QStringLiteral("appWindow"));
    setWindowTitle(QStringLiteral("Speecher"));
    buildSharedPages();
    connect(m_pages, &SettingsPageSet::changed, m_dictation, &DictationPage::refreshSummary);
    connect(m_dictation, &DictationPage::navigateRequested, this, &AppWindow::navigateToSettings);

    if (m_prototype == QStringLiteral("b")) {
        buildToolbarShell();
    } else if (m_prototype == QStringLiteral("c")) {
        buildCompactShell();
    } else {
        m_prototype = QStringLiteral("a");
        buildSidebarShell();
    }

    const QByteArray geometry = m_controller->settings()->raw()
                                    .value(QStringLiteral("ui/appWindow/%1/geometry").arg(m_prototype))
                                    .toByteArray();
    if (!geometry.isEmpty()) {
        restoreGeometry(geometry);
    }
}

QString AppWindow::prototype() const { return m_prototype; }

QStringList AppWindow::pageTitles() const
{
    QStringList titles;
    for (const auto &page : kPages) {
        titles << page.first;
    }
    return titles;
}

int AppWindow::pageCount() const { return kPages.size(); }

void AppWindow::navigateToSettings(int page)
{
    const int boundedPage = qBound(0, page, kPages.size() - 2);
    if (m_prototype == QStringLiteral("c")) {
        showCompactPage(boundedPage);
    } else if (m_stack) {
        m_stack->setCurrentIndex(boundedPage + 1);
    }
}

void AppWindow::closeEvent(QCloseEvent *event)
{
    rememberGeometry();
    QMainWindow::closeEvent(event);
}

void AppWindow::buildSharedPages()
{
    auto *refinementContent = new QWidget(this);
    auto *refinementLayout = new QVBoxLayout(refinementContent);
    refinementLayout->addWidget(settings::makeSectionLabel(QStringLiteral("Refinement"), refinementContent));
    refinementLayout->addWidget(detachedContent(m_pages->refinement()));
    refinementLayout->addWidget(settings::makeSectionLabel(QStringLiteral("Provider accounts"), refinementContent));
    refinementLayout->addWidget(detachedContent(m_pages->providers()));
    refinementLayout->addStretch();
    QWidget *refinement = scrollingPage(refinementContent, this);

    auto *tabs = new QTabWidget(this);
    auto addTab = [tabs](QWidget *page, const QString &title) {
        tabs->addTab(scrollingPage(page, tabs), title);
    };
    addTab(m_pages->vocabulary(), QStringLiteral("Vocabulary"));
    addTab(m_pages->corrections(), QStringLiteral("Learned corrections"));
    auto *bindingsScroll = scrollingPage(m_pages->bindings(), tabs);
    tabs->addTab(bindingsScroll, QStringLiteral("Replacements & snippets"));
    connect(m_pages->bindings(), &BindingsSettingsPage::preserveScrollRequested,
            bindingsScroll, [bindingsScroll](bool rebuilding) {
                if (rebuilding) {
                    bindingsScroll->setProperty("preservedScroll",
                                                bindingsScroll->verticalScrollBar()->value());
                } else {
                    QTimer::singleShot(0, bindingsScroll, [bindingsScroll] {
                        bindingsScroll->verticalScrollBar()->setValue(
                            bindingsScroll->property("preservedScroll").toInt());
                    });
                }
            });

    m_pageWidgets = {
        m_dictation,
        m_pages->general(),
        m_pages->audio(),
        m_pages->applications(),
        m_pages->output(),
        refinement,
        tabs,
    };
}

void AppWindow::buildSidebarShell()
{
    resize(900, 640);
    setMinimumSize(760, 520);
    auto *central = new QWidget(this);
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    auto *body = new QWidget(central);
    auto *bodyLayout = new QHBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    auto *rail = new QListWidget(body);
    rail->setObjectName(QStringLiteral("appNavigation"));
    rail->setFrameShape(QFrame::NoFrame);
    rail->setFixedWidth(200);
    for (const auto &page : kPages) {
        auto *item = new QListWidgetItem(QIcon::fromTheme(page.second), page.first, rail);
        item->setSizeHint(QSize(0, 40));
    }
    m_stack = new QStackedWidget(body);
    m_stack->setObjectName(QStringLiteral("appPageStack"));
    for (QWidget *page : std::as_const(m_pageWidgets)) {
        m_stack->addWidget(page);
    }
    auto *right = new QWidget(body);
    auto *rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    m_autoSaveWarning = new QFrame(right);
    m_autoSaveWarning->setObjectName(QStringLiteral("autoSaveWarning"));
    m_autoSaveWarning->setFrameShape(QFrame::StyledPanel);
    auto *warningLayout = new QHBoxLayout(m_autoSaveWarning);
    warningLayout->addWidget(new QLabel(QStringLiteral("Fix invalid replacement rules to save"), m_autoSaveWarning));
    m_autoSaveWarning->hide();
    rightLayout->addWidget(m_autoSaveWarning);
    rightLayout->addWidget(m_stack, 1);
    bodyLayout->addWidget(rail);
    bodyLayout->addWidget(right, 1);
    root->addWidget(body, 1);
    root->addWidget(createPrototypeSwitcher(central));
    setCentralWidget(central);
    rail->setCurrentRow(0);
    connect(rail, &QListWidget::currentRowChanged, m_stack, &QStackedWidget::setCurrentIndex);

    m_autoSaveTimer = new QTimer(this);
    m_autoSaveTimer->setSingleShot(true);
    m_autoSaveTimer->setInterval(600);
    connect(m_pages, &SettingsPageSet::changed, m_autoSaveTimer, qOverload<>(&QTimer::start));
    connect(m_autoSaveTimer, &QTimer::timeout, this, [this] {
        m_autoSaveWarning->setVisible(!m_pages->save(false, false));
        m_dictation->refreshSummary();
    });
}

void AppWindow::buildToolbarShell()
{
    resize(900, 640);
    setMinimumSize(760, 520);
    auto *toolbar = addToolBar(QStringLiteral("Pages"));
    toolbar->setMovable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    auto *actions = new QActionGroup(this);
    actions->setExclusive(true);
    for (int index = 0; index < kPages.size(); ++index) {
        const auto &page = kPages.at(index);
        QAction *action = toolbar->addAction(QIcon::fromTheme(page.second), page.first);
        action->setCheckable(true);
        actions->addAction(action);
        connect(action, &QAction::triggered, this, [this, index] { m_stack->setCurrentIndex(index); });
        if (index == 0) action->setChecked(true);
    }

    auto *central = new QWidget(this);
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    m_stack = new QStackedWidget(central);
    m_stack->setObjectName(QStringLiteral("appPageStack"));
    for (QWidget *page : std::as_const(m_pageWidgets)) m_stack->addWidget(page);
    root->addWidget(m_stack, 1);
    root->addWidget(createPendingBanner(central));
    root->addWidget(createPrototypeSwitcher(central));
    setCentralWidget(central);
    connect(m_pages, &SettingsPageSet::changed, this, &AppWindow::updatePendingBanner);
}

void AppWindow::buildCompactShell()
{
    resize(420, 560);
    setMinimumSize(400, 480);
    setMaximumWidth(520);
    auto *central = new QWidget(this);
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    m_compactStack = new QStackedWidget(central);

    auto *home = new QWidget(m_compactStack);
    auto *homeLayout = new QVBoxLayout(home);
    homeLayout->setContentsMargins(0, 0, 0, 0);
    homeLayout->addWidget(m_dictation, 1);
    m_compactList = new QListWidget(home);
    m_compactList->setObjectName(QStringLiteral("compactSettingsList"));
    m_compactList->setFrameShape(QFrame::NoFrame);
    for (int index = 1; index < kPages.size(); ++index) {
        const auto &page = kPages.at(index);
        auto *item = new QListWidgetItem(QIcon::fromTheme(page.second), page.first + QStringLiteral("    ›"), m_compactList);
        item->setSizeHint(QSize(0, 42));
    }
    homeLayout->addWidget(m_compactList);

    auto *drill = new QWidget(m_compactStack);
    auto *drillLayout = new QVBoxLayout(drill);
    auto *header = new QWidget(drill);
    auto *headerLayout = new QHBoxLayout(header);
    auto *back = new QPushButton(QStringLiteral("‹ Back"), header);
    m_drillTitle = new QLabel(header);
    QFont titleFont = m_drillTitle->font();
    titleFont.setBold(true);
    m_drillTitle->setFont(titleFont);
    headerLayout->addWidget(back);
    headerLayout->addWidget(m_drillTitle);
    headerLayout->addStretch();
    drillLayout->addWidget(header);
    m_drillPages = new QStackedWidget(drill);
    for (int index = 1; index < m_pageWidgets.size(); ++index) m_drillPages->addWidget(m_pageWidgets.at(index));
    drillLayout->addWidget(m_drillPages, 1);
    drillLayout->addWidget(createPendingBanner(drill, true));

    m_compactStack->addWidget(home);
    m_compactStack->addWidget(drill);
    root->addWidget(m_compactStack, 1);
    root->addWidget(createPrototypeSwitcher(central));
    setCentralWidget(central);
    connect(m_compactList, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
        showCompactPage(m_compactList->row(item));
    });
    connect(m_compactList, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        showCompactPage(m_compactList->row(item));
    });
    connect(back, &QPushButton::clicked, this, [this] {
        if (m_pages->hasChanges()) {
            m_pendingCompactBack = true;
            updatePendingBanner();
        } else {
            finishCompactBack();
        }
    });
}

QWidget *AppWindow::createPrototypeSwitcher(QWidget *parent)
{
    auto *bar = new QWidget(parent);
    auto *layout = new QHBoxLayout(bar);
    layout->setContentsMargins(8, 4, 8, 4);
    layout->addStretch();
    auto *label = new QLabel(QStringLiteral("UI:"), bar);
    auto *combo = new QComboBox(bar);
    combo->setObjectName(QStringLiteral("uiPrototypeSwitcher"));
    combo->addItem(QStringLiteral("A"), QStringLiteral("a"));
    combo->addItem(QStringLiteral("B"), QStringLiteral("b"));
    combo->addItem(QStringLiteral("C"), QStringLiteral("c"));
    combo->addItem(QStringLiteral("Legacy"), QStringLiteral("legacy"));
    combo->setCurrentIndex(combo->findData(m_prototype));
    QFont font = combo->font();
    font.setPointSize(qMax(7, font.pointSize() - 1));
    combo->setFont(font);
    label->setFont(font);
    layout->addWidget(label);
    layout->addWidget(combo);
    connect(combo, &QComboBox::currentIndexChanged, this, [this, combo] {
        m_controller->switchUiPrototype(combo->currentData().toString());
    });
    return bar;
}

QWidget *AppWindow::createPendingBanner(QWidget *parent, bool compact)
{
    m_pendingBanner = new QFrame(parent);
    m_pendingBanner->setObjectName(QStringLiteral("pendingChangesBanner"));
    m_pendingBanner->setFrameShape(QFrame::StyledPanel);
    auto *layout = new QHBoxLayout(m_pendingBanner);
    layout->setContentsMargins(compact ? 6 : 12, 6, compact ? 6 : 12, 6);
    layout->addWidget(new QLabel(QStringLiteral("You have unsaved changes"), m_pendingBanner));
    layout->addStretch();
    auto *apply = new QPushButton(QStringLiteral("Apply"), m_pendingBanner);
    auto *discard = new QPushButton(QStringLiteral("Discard"), m_pendingBanner);
    layout->addWidget(apply);
    layout->addWidget(discard);
    m_pendingBanner->hide();
    connect(apply, &QPushButton::clicked, this, [this] {
        if (m_pages->save()) {
            m_pendingBanner->hide();
            m_dictation->refreshSummary();
            if (m_pendingCompactBack) finishCompactBack();
        }
    });
    connect(discard, &QPushButton::clicked, this, [this] {
        m_pages->load();
        m_pendingBanner->hide();
        if (m_pendingCompactBack) finishCompactBack();
    });
    return m_pendingBanner;
}

void AppWindow::updatePendingBanner()
{
    if (m_pendingBanner) m_pendingBanner->setVisible(m_pages->hasChanges());
}

void AppWindow::showCompactPage(int page)
{
    const int boundedPage = qBound(0, page, kPages.size() - 2);
    m_drillPages->setCurrentIndex(boundedPage);
    m_drillTitle->setText(kPages.at(boundedPage + 1).first);
    m_compactStack->setCurrentIndex(1);
    m_pendingCompactBack = false;
    m_pendingBanner->hide();
}

void AppWindow::finishCompactBack()
{
    m_pendingCompactBack = false;
    m_compactStack->setCurrentIndex(0);
}

void AppWindow::rememberGeometry()
{
    m_controller->settings()->raw().setValue(
        QStringLiteral("ui/appWindow/%1/geometry").arg(m_prototype), saveGeometry());
}

} // namespace speecher
