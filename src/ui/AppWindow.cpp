#include "ui/AppWindow.h"

#include "app/ApplicationController.h"
#include "app/UpdateController.h"
#include "core/SettingsStore.h"
#include "frontend/qt/SchemaSettingsPage.h"
#include "ui/DictationPage.h"
#include "ui/settings/SettingsPageSet.h"
#include "ui/settings/SettingsPageSupport.h"

#include <QCloseEvent>
#include <QEvent>
#include <QApplication>
#include <QCheckBox>
#include <QFileSystemWatcher>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMouseEvent>
#include <QWindow>
#include <QLineEdit>
#include <QListWidget>
#include <QPalette>
#include <QPushButton>
#include <QProgressBar>
#include <QScrollArea>
#include <QShortcut>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStyle>
#include <QTabWidget>
#include <QTextDocument>
#include <QTimer>
#include <QVBoxLayout>

#include <utility>

#ifdef Q_OS_MACOS
#include "platform/mac/MacWindowChrome.h"
#endif

namespace speecher {

namespace {

#ifdef Q_OS_MACOS
// With the titlebar hidden the traffic lights float over the header strip, so
// the sidebar's search field starts below them instead of at the window edge.
constexpr int kTrafficLightInset = 28;
#endif

struct PageDefinition {
    QString title;
    QString iconName;
    QString fallbackIconName;
};

const QList<PageDefinition> kPages{
    {QStringLiteral("Dictation"), QStringLiteral("audio-input-microphone"), QString()},
    {QStringLiteral("General"), QStringLiteral("preferences-system"), QString()},
    {QStringLiteral("Audio"), QStringLiteral("preferences-desktop-sound"), QString()},
    {QStringLiteral("Applications"), QStringLiteral("preferences-desktop-default-applications"), QString()},
    {QStringLiteral("Output"), QStringLiteral("klipper"), QStringLiteral("edit-paste")},
    {QStringLiteral("Auth"), QStringLiteral("preferences-desktop-user-password"), QStringLiteral("dialog-password")},
    {QStringLiteral("Refinement"), QStringLiteral("tools-wizard"), QStringLiteral("document-edit")},
    {QStringLiteral("Vocabulary"), QStringLiteral("accessories-dictionary"), QStringLiteral("tools-check-spelling")},
};

QScrollArea *scrollingPage(QWidget *content, QWidget *parent)
{
    auto *scroll = new QScrollArea(parent);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setBackgroundRole(QPalette::Window);
    scroll->viewport()->setBackgroundRole(QPalette::Window);
    content->setAutoFillBackground(false);
    scroll->setWidget(content);
    return scroll;
}

void removeEmbeddedPageTitle(QWidget *content)
{
    QLabel *title = content->findChild<QLabel *>(QStringLiteral("pageTitle"));
    if (!title || !title->parentWidget() || !title->parentWidget()->layout()) {
        return;
    }
    QLayout *layout = title->parentWidget()->layout();
    const int index = layout->indexOf(title);
    delete layout->takeAt(index);
    delete title;
    if (QLayoutItem *gap = layout->takeAt(index)) {
        delete gap;
    }
}

QWidget *detachedContent(QScrollArea *page, bool removeTitle = false)
{
    QWidget *content = page->takeWidget();
    if (removeTitle) {
        removeEmbeddedPageTitle(content);
    }
    content->layout()->unsetContentsMargins();
    page->hide();
    return content;
}

QIcon pageIcon(const PageDefinition &page, QWidget *widget)
{
    const QIcon icon = QIcon::fromTheme(
        page.iconName, QIcon::fromTheme(page.fallbackIconName));
    return icon.isNull() ? widget->style()->standardIcon(QStyle::SP_FileIcon) : icon;
}

} // namespace

AppWindow::AppWindow(ApplicationController *controller, QWidget *parent)
    : QMainWindow(parent)
    , m_controller(controller)
    , m_pages(new SettingsPageSet(controller, this))
    , m_dictation(new DictationPage(controller, this))
{
    setObjectName(QStringLiteral("appWindow"));
    setWindowTitle(QStringLiteral("Speecher"));
    buildSharedPages();
    connect(m_pages, &SettingsPageSet::changed, m_dictation, &DictationPage::refreshSummary);
    connect(m_pages, &SettingsPageSet::whatsNewRequested, this, &AppWindow::showWhatsNew);
    connect(m_dictation, &DictationPage::navigateRequested, this, &AppWindow::navigateToSettings);
    connect(m_controller->updates(),
            &UpdateController::changed,
            this,
            &AppWindow::refreshUpdateBanner);
    connect(m_controller,
            &ApplicationController::whatsNewChanged,
            this,
            &AppWindow::refreshUpdateBanner);

    buildSidebarShell();
    settings::applyLabelHierarchy(this);

    const QByteArray geometry = m_controller->settings()->raw()
                                    .value(QStringLiteral("ui/appWindow/a/geometry"))
                                    .toByteArray();
    if (!geometry.isEmpty()) {
        restoreGeometry(geometry);
    }
}

QStringList AppWindow::pageTitles() const
{
    QStringList titles;
    for (const auto &page : kPages) {
        titles << page.title;
    }
    return titles;
}

int AppWindow::pageCount() const { return kPages.size(); }

void AppWindow::navigateToSettings(AppPageId page)
{
    const int settingsIndex = static_cast<int>(page);
    for (int row = 0; row < m_navigation->count(); ++row) {
        QListWidgetItem *item = m_navigation->item(row);
        if (item->data(Qt::UserRole).toInt() == settingsIndex + 1) {
            m_navigation->setCurrentItem(item);
            break;
        }
    }
}

void AppWindow::refreshHeaderStripColor()
{
    // Every hairline (header divider, header underline, sidebar splitter
    // handle) uses the same color, derived from the same palette, so no line
    // reads lighter than its neighbors.
#ifdef Q_OS_MACOS
    const QPalette headerPalette = palette();
#else
    const QPalette headerPalette = settings::kdeHeaderPalette(palette());
#endif
    const QColor line = settings::separatorColor(headerPalette);
    if (m_headerStrip) {
        m_headerStrip->setPalette(headerPalette);
    }
    for (QWidget *hairline : {m_headerDividerLine, m_headerUnderline}) {
        if (hairline) {
            QPalette linePalette(hairline->palette());
            linePalette.setColor(QPalette::Window, line);
            hairline->setPalette(linePalette);
        }
    }
    // Fill the 1px splitter handle: unstyled it stays unpainted, which shows
    // as a see-through seam between the sidebar and the content.
    if (m_sidebarSplitter) {
        m_sidebarSplitter->setStyleSheet(
            QStringLiteral("QSplitter#sidebarSplitter::handle{background:%1;}")
                .arg(line.name(QColor::HexRgb)));
    }
}

void AppWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::PaletteChange
        || event->type() == QEvent::ApplicationPaletteChange
        || event->type() == QEvent::ThemeChange) {
        refreshHeaderStripColor();
    }
}

void AppWindow::flushPendingAutoSave()
{
    if (!m_autoSaveTimer || !m_autoSaveTimer->isActive()) return;
    m_autoSaveTimer->stop();
    runAutoSave();
}

void AppWindow::closeEvent(QCloseEvent *event)
{
    flushPendingAutoSave();
    rememberGeometry();
    QMainWindow::closeEvent(event);
}

void AppWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    if (m_pageLoadScheduled) {
        return;
    }
    m_pageLoadScheduled = true;
    {
        const QSignalBlocker blocker(m_pages);
        m_pages->loadBeforeShow();
    }
}

void AppWindow::paintEvent(QPaintEvent *event)
{
    QMainWindow::paintEvent(event);
    if (!m_pageLoadScheduled || m_afterShowLoadScheduled) {
        return;
    }
    m_afterShowLoadScheduled = true;
    QTimer::singleShot(0, this, [this] {
        m_pages->loadAfterShow();
        m_dictation->refreshSummary();
    });
}

bool AppWindow::eventFilter(QObject *watched, QEvent *event)
{
    // Keep the header's search section exactly as wide as the sidebar pane,
    // whatever resizes it (layout settling, splitter drag, window resize).
    if (watched == m_sidebarPane && event->type() == QEvent::Resize
        && m_searchSection) {
        m_searchSection->setFixedWidth(m_sidebarPane->width());
#ifdef Q_OS_MACOS
        mac::updateSidebarWidth(this, m_sidebarPane->width());
#endif
    }
    // The header strip reads as part of the title bar, so empty space in it
    // must drag and double-click the window like the title bar does. Only
    // events the interactive children ignore bubble up to the strip itself.
    if (watched == m_headerStrip) {
        if (event->type() == QEvent::MouseButtonPress
            && static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton) {
            m_headerDragPending = true;
            m_headerPressPosition = static_cast<QMouseEvent *>(event)->position().toPoint();
        }
        if (event->type() == QEvent::MouseMove && m_headerDragPending) {
            const auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (!(mouseEvent->buttons() & Qt::LeftButton)) {
                m_headerDragPending = false;
            } else if ((mouseEvent->position().toPoint() - m_headerPressPosition).manhattanLength()
                           >= QApplication::startDragDistance()) {
                m_headerDragPending = false;
                if (windowHandle() && windowHandle()->startSystemMove()) {
                    return true;
                }
            }
        }
        if (event->type() == QEvent::MouseButtonRelease
            && static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton) {
            m_headerDragPending = false;
        }
        if (event->type() == QEvent::MouseButtonDblClick
            && static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton) {
            m_headerDragPending = false;
            isMaximized() ? showNormal() : showMaximized();
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void AppWindow::buildSharedPages()
{
    auto *refinementContent = new QWidget(this);
    auto *refinementLayout = new QVBoxLayout(refinementContent);
    settings::applyPageMargins(refinementLayout);
    refinementLayout->setSpacing(0);
    refinementLayout->addWidget(settings::makePageTitle(QStringLiteral("Refinement"), refinementContent));
    refinementLayout->addSpacing(settings::sectionGap());
    refinementLayout->addWidget(detachedContent(m_pages->refinement(), true));
    refinementLayout->addSpacing(settings::groupGap());
    refinementLayout->addWidget(detachedContent(m_pages->providerModels(), true));
    refinementLayout->addStretch();
    QWidget *refinement = scrollingPage(refinementContent, this);

    auto *authContent = new QWidget(this);
    auto *authLayout = new QVBoxLayout(authContent);
    settings::applyPageMargins(authLayout);
    authLayout->setSpacing(0);
    authLayout->addWidget(settings::makePageTitle(QStringLiteral("Auth"), authContent));
    authLayout->addSpacing(settings::sectionGap());
    authLayout->addWidget(detachedContent(m_pages->providerAuth(), true));
    authLayout->addStretch();
    QWidget *auth = scrollingPage(authContent, this);

    auto *tabs = new QTabWidget(this);
    const auto addTab = [tabs](QScrollArea *page, const QString &title) {
        QWidget *content = detachedContent(page, true);
        settings::applyPageMargins(content->layout());
        auto *scroll = scrollingPage(content, tabs);
        tabs->addTab(scroll, title);
        return scroll;
    };
    addTab(m_pages->vocabulary(), QStringLiteral("Vocabulary"));
    addTab(m_pages->corrections(), QStringLiteral("Learned corrections"));
    m_pages->preserveBindingScroll(
        addTab(m_pages->bindings(), QStringLiteral("Replacements && snippets")));

    auto *vocabularyContent = new QWidget(this);
    auto *vocabularyLayout = new QVBoxLayout(vocabularyContent);
    settings::applyPageMargins(vocabularyLayout);
    vocabularyLayout->setSpacing(0);
    vocabularyLayout->addWidget(settings::makePageTitle(QStringLiteral("Vocabulary"), vocabularyContent));
    vocabularyLayout->addSpacing(settings::sectionGap());
    vocabularyLayout->addWidget(tabs, 1);

    m_pageWidgets = {
        m_dictation,
        m_pages->general(),
        m_pages->audio(),
        m_pages->applications(),
        m_pages->output(),
        auth,
        refinement,
        vocabularyContent,
        m_pages->whatsNew(),
    };
    for (QWidget *page : std::as_const(m_pageWidgets)) {
        removeEmbeddedPageTitle(page);
    }
}

void AppWindow::buildSidebarShell()
{
    resize(900, 640);
    setMinimumSize(760, 520);
    auto *central = new QWidget(this);
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    auto *header = new QWidget(central);
    header->setObjectName(QStringLiteral("sidebarHeaderStrip"));
    m_headerStrip = header;
    refreshHeaderStripColor();
    header->setBackgroundRole(QPalette::Window);
    header->setAutoFillBackground(true);
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(0);

    auto *searchContainer = new QWidget(header);
    searchContainer->setObjectName(QStringLiteral("sidebarSearchContainer"));
    auto *searchLayout = new QHBoxLayout(searchContainer);
    searchLayout->setContentsMargins(settings::relatedSpacing(),
                                     settings::relatedSpacing(),
                                     settings::relatedSpacing(),
                                     settings::relatedSpacing());
    auto *search = new QLineEdit(searchContainer);
    search->setObjectName(QStringLiteral("appSearch"));
    search->setPlaceholderText(QStringLiteral("Search…"));
    search->setClearButtonEnabled(true);
    search->addAction(QIcon::fromTheme(
                          QStringLiteral("search"),
                          QIcon::fromTheme(QStringLiteral("edit-find"))),
                      QLineEdit::LeadingPosition);
    searchLayout->addWidget(search);
    headerLayout->addWidget(searchContainer);

    // Short floating divider at the sidebar boundary, inset from the strip's
    // top and bottom edges — the System Settings header treatment.
    auto *headerDivider = new QWidget(header);
    auto *dividerLayout = new QVBoxLayout(headerDivider);
    dividerLayout->setContentsMargins(0, settings::relatedSpacing(), 0, settings::relatedSpacing());
    auto *dividerLine = new QWidget(headerDivider);
    dividerLine->setFixedWidth(1);
    m_headerDividerLine = dividerLine;
    dividerLine->setAutoFillBackground(true);
    dividerLayout->addWidget(dividerLine);
    headerLayout->addWidget(headerDivider);

    auto *headerRight = new QWidget(header);
    auto *headerRightLayout = new QHBoxLayout(headerRight);
    headerRightLayout->setContentsMargins(settings::relatedSpacing(),
                                          settings::relatedSpacing(),
                                          settings::relatedSpacing(),
                                          settings::relatedSpacing());
    m_pageTitle = settings::makePageTitle(kPages.first().title, headerRight);
    headerRightLayout->addWidget(m_pageTitle);
    headerRightLayout->addStretch();
    headerLayout->addWidget(headerRight, 1);
    header->installEventFilter(this);
    root->addWidget(header);

#ifndef Q_OS_MACOS
    auto *colorConfigWatcher = new QFileSystemWatcher(this);
    const QString kdeGlobals =
        QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/kdeglobals");
    colorConfigWatcher->addPath(kdeGlobals);
    connect(colorConfigWatcher,
            &QFileSystemWatcher::fileChanged,
            this,
            [this, colorConfigWatcher](const QString &path) {
                colorConfigWatcher->addPath(path);
                QTimer::singleShot(0, this, &AppWindow::refreshHeaderStripColor);
            });
#endif

    // Same fill mechanism and color as the splitter handle, so the strip's
    // bottom edge and the sidebar/content hairline match exactly.
    auto *headerUnderline = new QWidget(central);
    headerUnderline->setFixedHeight(1);
    m_headerUnderline = headerUnderline;
    headerUnderline->setAutoFillBackground(true);
    root->addWidget(headerUnderline);

    m_sidebarSplitter = new QSplitter(Qt::Horizontal, central);
    m_sidebarSplitter->setObjectName(QStringLiteral("sidebarSplitter"));
    m_sidebarSplitter->setHandleWidth(1);
    m_sidebarSplitter->setChildrenCollapsible(false);
    auto *sidebar = new QWidget(m_sidebarSplitter);
    sidebar->setBackgroundRole(QPalette::Base);
    sidebar->setAutoFillBackground(true);
    sidebar->setMinimumWidth(180);
    sidebar->setMaximumWidth(320);
    auto *sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(settings::relatedSpacing(),
                                      settings::relatedSpacing(),
                                      settings::relatedSpacing(),
                                      0);
    sidebarLayout->setSpacing(0);
    m_navigation = new QListWidget(sidebar);
    m_navigation->setObjectName(QStringLiteral("appNavigation"));
    m_navigation->setBackgroundRole(QPalette::Base);
    m_navigation->setAutoFillBackground(true);
    m_navigation->viewport()->setBackgroundRole(QPalette::Base);
    m_navigation->viewport()->setAutoFillBackground(true);
    m_navigation->setFrameShape(QFrame::NoFrame);
    m_navigation->setSpacing(2);
    m_navigation->setIconSize(QSize(22, 22));
    for (int index = 0; index < kPages.size(); ++index) {
        const auto &page = kPages.at(index);
        auto *item = new QListWidgetItem(pageIcon(page, m_navigation),
                                         page.title,
                                         m_navigation);
        item->setData(Qt::UserRole, index);
        item->setSizeHint(QSize(0, 32));
    }
    sidebarLayout->addWidget(m_navigation, 1);
    m_stack = new QStackedWidget(m_sidebarSplitter);
    m_stack->setObjectName(QStringLiteral("appPageStack"));
    for (QWidget *page : std::as_const(m_pageWidgets)) {
        m_stack->addWidget(page);
    }
    auto *right = new QWidget(m_sidebarSplitter);
    right->setBackgroundRole(QPalette::Window);
    right->setAutoFillBackground(true);
    right->setMinimumWidth(480);
    auto *rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    m_updateBanner = new QFrame(right);
    m_updateBanner->setObjectName(QStringLiteral("updateBanner"));
    m_updateBanner->setFrameShape(QFrame::StyledPanel);
    auto *updateLayout = new QHBoxLayout(m_updateBanner);
    updateLayout->setContentsMargins(settings::relatedSpacing(),
                                     settings::tightSpacing(),
                                     settings::relatedSpacing(),
                                     settings::tightSpacing());
    m_updateBannerText = new QLabel(m_updateBanner);
    updateLayout->addWidget(m_updateBannerText, 1);
    m_updateProgress = new QProgressBar(m_updateBanner);
    m_updateProgress->setRange(0, 100);
    m_updateProgress->setTextVisible(true);
    m_updateProgress->setMaximumWidth(160);
    updateLayout->addWidget(m_updateProgress);
    m_updateAction = new QPushButton(m_updateBanner);
    updateLayout->addWidget(m_updateAction);
    m_updateLater = new QPushButton(QStringLiteral("Later"), m_updateBanner);
    updateLayout->addWidget(m_updateLater);
    m_updateDismiss = new QPushButton(QStringLiteral("×"), m_updateBanner);
    m_updateDismiss->setAccessibleName(QStringLiteral("Dismiss update"));
    m_updateDismiss->setFlat(true);
    updateLayout->addWidget(m_updateDismiss);
    connect(m_updateAction, &QPushButton::clicked, this, [this] {
        if (m_showingWhatsNewBanner) {
            showWhatsNew();
        } else {
            m_controller->updates()->updateNow();
        }
    });
    connect(m_updateLater, &QPushButton::clicked, this, [this] {
        m_updateBannerDeferred = true;
        m_updateBanner->hide();
    });
    connect(m_updateDismiss, &QPushButton::clicked, this, [this] {
        if (m_showingWhatsNewBanner) {
            m_controller->clearPendingWhatsNew();
        } else {
            m_controller->updates()->dismissAvailableVersion();
        }
    });
    rightLayout->addWidget(m_updateBanner);
    refreshUpdateBanner();
    m_autoSaveWarning = new QFrame(right);
    m_autoSaveWarning->setObjectName(QStringLiteral("autoSaveWarning"));
    m_autoSaveWarning->setFrameShape(QFrame::StyledPanel);
    auto *warningLayout = new QHBoxLayout(m_autoSaveWarning);
    m_autoSaveWarningText = new QLabel(m_autoSaveWarning);
    warningLayout->addWidget(m_autoSaveWarningText);
    m_autoSaveWarning->hide();
    rightLayout->addWidget(m_autoSaveWarning);
    rightLayout->addWidget(m_stack, 1);
    m_sidebarSplitter->addWidget(sidebar);
    m_sidebarSplitter->addWidget(right);
    m_sidebarSplitter->setStretchFactor(0, 0);
    m_sidebarSplitter->setStretchFactor(1, 1);
    if (QWidget *handle = m_sidebarSplitter->handle(1)) {
        // Breeze paints splitter handles in plain window color — invisible.
        // Fill the 1px handle with the separator blend so the sidebar/content
        // boundary reads as a hairline, like System Settings.
        QPalette handlePalette(handle->palette());
        handlePalette.setColor(QPalette::Window,
                               settings::separatorColor(handle->palette()));
        handle->setPalette(handlePalette);
        handle->setAutoFillBackground(true);
    }
    m_sidebarPane = sidebar;
    m_searchSection = searchContainer;
    sidebar->installEventFilter(this);
    connect(m_sidebarSplitter, &QSplitter::splitterMoved, searchContainer,
            [searchContainer, sidebar] { searchContainer->setFixedWidth(sidebar->width()); });
    root->addWidget(m_sidebarSplitter, 1);
    setCentralWidget(central);
    // Re-run now that the hairline widgets exist; the first call above only
    // colored the strip itself.
    refreshHeaderStripColor();
    m_sidebarSplitter->setSizes({220, 680});
    const QByteArray splitterState = m_controller->settings()->raw()
                                         .value(QStringLiteral("ui/appWindow/a/splitter"))
                                         .toByteArray();
    if (!splitterState.isEmpty()) {
        m_sidebarSplitter->restoreState(splitterState);
    }
    searchContainer->setFixedWidth(sidebar->width());
#ifdef Q_OS_MACOS
    // The sidebar column is an NSVisualEffectView sitting behind Qt's content
    // view, so everything stacked over it has to stop painting for the blur to
    // reach the screen. The content column keeps its opaque window fill.
    setAttribute(Qt::WA_TranslucentBackground);
    header->setAutoFillBackground(false);
    sidebar->setAutoFillBackground(false);
    m_navigation->setAutoFillBackground(false);
    m_navigation->viewport()->setAutoFillBackground(false);
    for (QWidget *contentSide : {headerDivider, headerRight}) {
        contentSide->setBackgroundRole(QPalette::Window);
        contentSide->setAutoFillBackground(true);
    }
    searchLayout->setContentsMargins(settings::relatedSpacing(),
                                     kTrafficLightInset,
                                     settings::relatedSpacing(),
                                     settings::relatedSpacing());
    mac::applyMainWindowChrome(this, sidebar->width());
#endif
    m_navigation->setCurrentRow(0);
    connect(m_navigation, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem *item) {
                if (item && item->data(Qt::UserRole).toInt() >= 0) {
                    m_stack->setCurrentIndex(item->data(Qt::UserRole).toInt());
                }
            });
    connect(m_stack, &QStackedWidget::currentChanged, this, [this](int index) {
        m_pageTitle->setText(index < kPages.size()
                                 ? kPages.at(index).title
                                 : QStringLiteral("What's New"));
    });
    connect(search, &QLineEdit::textChanged, this, &AppWindow::filterSidebarPages);
    connect(search, &QLineEdit::returnPressed, this, [this] {
        for (int row = 0; row < m_navigation->count(); ++row) {
            if (!m_navigation->item(row)->isHidden()) {
                m_navigation->setCurrentRow(row);
                return;
            }
        }
    });
    auto *clearSearch = new QShortcut(QKeySequence(Qt::Key_Escape), search);
    clearSearch->setContext(Qt::WidgetShortcut);
    connect(clearSearch, &QShortcut::activated, search, &QLineEdit::clear);

    m_autoSaveTimer = new QTimer(this);
    m_autoSaveTimer->setSingleShot(true);
    m_autoSaveTimer->setInterval(600);
    connect(m_pages, &SettingsPageSet::changed, m_autoSaveTimer, qOverload<>(&QTimer::start));
    connect(m_autoSaveTimer, &QTimer::timeout, this, &AppWindow::runAutoSave);
}

void AppWindow::refreshUpdateBanner()
{
    if (!m_updateBanner) {
        return;
    }
    UpdateController *updates = m_controller->updates();
    const QString availableVersion = updates->availableVersion();
    if ((!availableVersion.isEmpty() && m_updateBannerVersion != availableVersion)
        || m_updateBannerInstalledVersion != updates->currentVersion()) {
        m_updateBannerDeferred = false;
        m_updateBannerVersion = availableVersion;
        m_updateBannerInstalledVersion = updates->currentVersion();
    }
    if (!updates->bannerVisible() && !m_controller->pendingWhatsNewVersion().isEmpty()) {
        m_showingWhatsNewBanner = true;
        m_updateBanner->show();
        m_updateBannerText->setText(
            QStringLiteral("Speecher %1 is installed")
                .arg(updates->currentVersion().section(QLatin1Char('-'), 0, 0)));
        m_updateProgress->hide();
        m_updateAction->setText(QStringLiteral("See what's new"));
        m_updateAction->setEnabled(true);
        m_updateAction->show();
        m_updateLater->hide();
        m_updateDismiss->show();
        return;
    }
    m_showingWhatsNewBanner = false;
    if (m_updateBannerDeferred
        && (updates->state() == UpdateController::State::ReadyToRestart
            || updates->state() == UpdateController::State::RestartPending)) {
        m_updateBanner->hide();
        return;
    }
    m_updateBanner->setVisible(updates->bannerVisible());
    if (!updates->bannerVisible()) {
        return;
    }

    m_updateProgress->hide();
    m_updateAction->show();
    m_updateAction->setEnabled(true);
    m_updateLater->hide();
    m_updateDismiss->hide();
    switch (updates->state()) {
    case UpdateController::State::UpdateAvailable:
        m_updateBannerText->setText(
            QStringLiteral("Speecher %1 is available").arg(updates->availableVersion()));
        m_updateAction->setText(QStringLiteral("Update now"));
        m_updateDismiss->show();
        break;
    case UpdateController::State::Downloading:
        m_updateBannerText->setText(
            QStringLiteral("Downloading Speecher %1").arg(updates->availableVersion()));
        m_updateProgress->setValue(updates->downloadPercent());
        m_updateProgress->show();
        m_updateAction->hide();
        m_updateDismiss->hide();
        break;
    case UpdateController::State::ReadyToRestart:
        m_updateBannerText->setText(updates->errorMessage().isEmpty()
                                        ? QStringLiteral("Restart to finish updating")
                                        : updates->errorMessage());
        m_updateAction->setText(QStringLiteral("Restart now"));
        m_updateLater->show();
        break;
    case UpdateController::State::RestartPending:
        m_updateBannerText->setText(QStringLiteral("Restarting after this dictation…"));
        m_updateAction->setText(QStringLiteral("Restarting after this dictation…"));
        m_updateAction->setEnabled(false);
        break;
    case UpdateController::State::Error:
        m_updateBannerText->setText(updates->errorMessage());
        m_updateAction->setText(QStringLiteral("Try again"));
        m_updateDismiss->show();
        break;
    default:
        m_updateBanner->hide();
        break;
    }
}

void AppWindow::showWhatsNew()
{
    m_navigation->setCurrentItem(nullptr);
    m_stack->setCurrentWidget(m_pages->whatsNew());
    m_controller->clearPendingWhatsNew();
}

void AppWindow::filterSidebarPages(const QString &query)
{
    if (query.isEmpty()) {
        for (int row = 0; row < m_navigation->count(); ++row) {
            m_navigation->item(row)->setHidden(false);
        }
        return;
    }

    if (m_pageKeywords.isEmpty()) {
        const auto cleanText = [](QString text) {
            text = text.trimmed();
            while (text.endsWith(QLatin1Char(':')) || text.endsWith(QChar(0x2026))) {
                text.chop(1);
                text = text.trimmed();
            }
            return text;
        };
        for (int pageIndex = 0; pageIndex < kPages.size(); ++pageIndex) {
            QStringList keywords{kPages.at(pageIndex).title};
            QWidget *page = m_pageWidgets.at(pageIndex);
            for (QLabel *label : page->findChildren<QLabel *>()) {
                if (!label->isHidden() && !Qt::mightBeRichText(label->text())) {
                    const QString text = cleanText(label->text());
                    if (!text.isEmpty()) keywords.append(text);
                }
            }
            for (QCheckBox *checkBox : page->findChildren<QCheckBox *>()) {
                if (!checkBox->isHidden()) {
                    const QString text = cleanText(checkBox->text());
                    if (!text.isEmpty()) keywords.append(text);
                }
            }
            for (QGroupBox *group : page->findChildren<QGroupBox *>()) {
                if (!group->isHidden()) {
                    const QString text = cleanText(group->title());
                    if (!text.isEmpty()) keywords.append(text);
                }
            }
            for (QPushButton *button : page->findChildren<QPushButton *>()) {
                if (!button->isHidden()) {
                    const QString text = cleanText(button->text());
                    if (!text.isEmpty()) keywords.append(text);
                }
            }
            m_pageKeywords.append(keywords.join(QLatin1Char('\n')));
        }
    }

    for (int row = 0; row < m_navigation->count(); ++row) {
        m_navigation->item(row)->setHidden(
            !m_pageKeywords.at(row).contains(query, Qt::CaseInsensitive));
    }
}

void AppWindow::runAutoSave()
{
    SettingsPageSet::SaveOutcome outcome;
    const bool saved = m_pages->save(false, false, &outcome);
    if (!saved) {
        m_autoSaveWarningText->setText(outcome.messages.join(QLatin1Char('\n')));
    }
    m_autoSaveWarning->setVisible(!saved);
    m_dictation->refreshSummary();
}

void AppWindow::rememberGeometry()
{
    m_controller->settings()->raw().setValue(
        QStringLiteral("ui/appWindow/a/geometry"), saveGeometry());
    if (m_sidebarSplitter) {
        m_controller->settings()->raw().setValue(
            QStringLiteral("ui/appWindow/a/splitter"), m_sidebarSplitter->saveState());
    }
}

} // namespace speecher
