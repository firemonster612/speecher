#include "ui/AppWindow.h"

#include "app/ApplicationController.h"
#include "app/UpdateBanner.h"
#include "app/UpdateController.h"
#include "core/SettingsStore.h"
#include "frontend/qt/SchemaSettingsPage.h"
#include "ui/HomePage.h"
#include "ui/InlineMessage.h"
#include "ui/TranscribePage.h"
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
#include <QMessageBox>
#include <QMouseEvent>
#include <QSettings>
#include <QSystemTrayIcon>
#include <QWindow>
#include <QLineEdit>
#include <QListWidget>
#include <QPalette>
#include <QPainter>
#include <QPushButton>
#include <QProgressBar>
#include <QScrollArea>
#include <QShortcut>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QStandardPaths>
#include <QStyle>
#include <QTabBar>
#include <QTextDocument>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
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

const QString kWhatsNewPane = QStringLiteral("whatsNew");
const QString kHomePane = QStringLiteral("home");
// The id a sidebar row carries. Spacer rows between runs carry none.
constexpr int kPaneRole = Qt::UserRole;

// Keeps an Alternatives pane's tab bar over the card column of the view below
// it, wherever that view's page centres its column.
class TabBarColumn final : public QObject {
public:
    TabBarColumn(QWidget *bar, QStackedWidget *views)
        : QObject(bar)
        , m_bar(bar)
        , m_views(views)
    {
        for (int index = 0; index < views->count(); ++index) {
            qobject_cast<QScrollArea *>(views->widget(index))->widget()->installEventFilter(this);
        }
        connect(views, &QStackedWidget::currentChanged, this, &TabBarColumn::follow);
    }

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::Move || event->type() == QEvent::Resize) {
            follow();
        }
        return QObject::eventFilter(watched, event);
    }

private:
    void follow()
    {
        // The stack empties as the window is torn down.
        auto *page = qobject_cast<QScrollArea *>(m_views->currentWidget());
        if (!page) {
            return;
        }
        QWidget *column = page->widget();
        const QMargins inner = column->layout()->contentsMargins();
        const int left = column->mapTo(m_views, QPoint(inner.left(), 0)).x();
        const int right = column->mapTo(m_views, QPoint(column->width() - inner.right(), 0)).x();
        m_bar->layout()->setContentsMargins(left, 0, m_views->width() - right, 0);
    }

    QWidget *m_bar;
    QStackedWidget *m_views;
};

QScrollArea *scrollingPage(QWidget *content, QWidget *parent)
{
    auto *scroll = new QScrollArea(parent);
    settings::configurePageScroll(scroll, content);
    return scroll;
}

// A pane's icon from the theme's monochrome action/status/device set, which
// Breeze draws in the text colour at the sidebar's 22px size. The
// preferences-* and app icons live in a different visual language (colourful,
// or gradients that stay dark on dark schemes), so one of them in the list
// makes the whole column read as mismatched.
QIcon paneIcon(const QString &iconId)
{
    static const QHash<QString, QStringList> names{
        {QStringLiteral("home"), {QStringLiteral("go-home"), QStringLiteral("user-home")}},
        {QStringLiteral("settings"), {QStringLiteral("settings-configure"), QStringLiteral("configure")}},
        {QStringLiteral("whatsNew"), {QStringLiteral("help-about")}},
        {QStringLiteral("microphone"), {QStringLiteral("audio-input-microphone")}},
        {QStringLiteral("refinement"), {QStringLiteral("tools-wizard"), QStringLiteral("document-edit")}},
        {QStringLiteral("writingProfiles"), {QStringLiteral("draw-text"), QStringLiteral("format-text-bold")}},
        {QStringLiteral("localModels"), {QStringLiteral("computer"), QStringLiteral("computer-laptop")}},
        {QStringLiteral("transcribe"), {QStringLiteral("view-media-lyrics"), QStringLiteral("document-import")}},
        {QStringLiteral("output"), {QStringLiteral("edit-paste"), QStringLiteral("edit-copy")}},
        // Breeze's spelling icon underlines in green, so it is the last resort.
        {QStringLiteral("vocabulary"),
         {QStringLiteral("font"), QStringLiteral("tools-check-spelling")}},
        {QStringLiteral("accounts"), {QStringLiteral("user-identity"), QStringLiteral("im-user")}},
    };
    for (const QString &name : names.value(iconId)) {
        if (QIcon::hasThemeIcon(name)) {
            return QIcon::fromTheme(name);
        }
    }
    return {};
}

// A theme without the icon leaves the row text-only: a stand-in document icon
// would say every page is a file.
// Tells the style where an item sits in its view. Qt's list views leave this
// unset, and Breeze only rounds a selection it knows is a whole item; nothing
// is painted here, the style does all the drawing.
class WholeItemDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter,
               const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        QStyleOptionViewItem whole(option);
        whole.viewItemPosition = QStyleOptionViewItem::OnlyOne;
        // The selected row names the visible page, so it keeps the active
        // highlight when the window loses focus instead of dimming.
        whole.state |= QStyle::State_Active;
        QStyledItemDelegate::paint(painter, whole, index);
    }
};

// Breeze paints its own splitter line over any palette fill, darker than
// every other hairline in the window. An empty paintEvent leaves only the
// autofilled palette colour, so the handle can match the other separators.
class HairlineSplitterHandle final : public QSplitterHandle {
public:
    using QSplitterHandle::QSplitterHandle;

protected:
    void paintEvent(QPaintEvent *) override {}
};

class HairlineSplitter final : public QSplitter {
public:
    using QSplitter::QSplitter;

protected:
    QSplitterHandle *createHandle() override
    {
        return new HairlineSplitterHandle(orientation(), this);
    }
};

} // namespace

AppWindow::AppWindow(ApplicationController *controller, QWidget *parent)
    : QMainWindow(parent)
    , m_controller(controller)
    , m_pages(new SettingsPageSet(controller, this))
    , m_home(new HomePage(controller, this))
    , m_transcribe(new TranscribePage(controller, this))
{
    setObjectName(QStringLiteral("appWindow"));
    setWindowTitle(QStringLiteral("Speecher"));
    buildPages();
    connect(m_pages, &SettingsPageSet::whatsNewRequested, this, &AppWindow::showWhatsNew);
    connect(m_pages, &SettingsPageSet::localModelsRequested, this,
            [this] { showPage(QStringLiteral("localModels")); });
    connect(m_pages, &SettingsPageSet::pageRequested, this, &AppWindow::showPage);
    connect(m_home, &HomePage::pageRequested, this, &AppWindow::showPage);
    connect(m_transcribe, &TranscribePage::pageRequested, this, &AppWindow::showPage);
    connect(m_controller->updateBanner(),
            &UpdateBanner::changed,
            this,
            &AppWindow::refreshUpdateBanner);
    connect(m_controller,
            &ApplicationController::whatsNewChanged,
            this,
            &AppWindow::refreshUpdateBanner);

    buildSidebarShell();
    connect(m_pages, &SettingsPageSet::settingsDeletionStarted, this, [this] {
        m_settingsDeletionStarted = true;
        m_autoSaveTimer->stop();
        m_autoSaveWarning->hide();
    });
    settings::applyLabelHierarchy(this);

    const QByteArray geometry = m_controller->settings()->raw()
                                    .value(QStringLiteral("ui/appWindow/a/geometry"))
                                    .toByteArray();
    if (!geometry.isEmpty()) {
        restoreGeometry(geometry);
    }
}

QStringList AppWindow::sidebarPanes() const
{
    QStringList panes;
    for (const SidebarGroup &group : m_pages->schema().sidebarGroups) {
        panes += group.panes;
    }
    return panes;
}

void AppWindow::showPage(const QString &pageId)
{
    const PageId page = resolvePage(m_pages->schema(), pageId);
    if (page.pane == kWhatsNewPane) {
        showWhatsNew();
        return;
    }
    if (QTabBar *tabs = m_viewTabs.value(page.pane)) {
        const auto *pane = m_pages->schema().pane(page.pane);
        for (int index = 0; index < pane->groups.size(); ++index) {
            if (pane->groups.at(index).view == page.view) {
                tabs->setCurrentIndex(index);
            }
        }
    }
    selectPane(page.pane);
    if (QWidget *subpage = m_subpageWidgets.value(page.subpage)) {
        m_stack->setCurrentWidget(subpage);
    }
}

void AppWindow::showSearchMatch(const SearchMatch &match, bool focusRow)
{
    showPage(match.pane);
    if (match.rows.isEmpty()) {
        return;
    }
    const QString &rowId = match.rows.first();
    QString pageId = match.pane;
    const SettingsPane *pane = m_pages->schema().pane(match.pane);
    if (pane->layout == PaneLayout::Alternatives) {
        for (const SettingsPaneGroup &group : pane->groups) {
            if (group.rows.contains(rowId)) {
                pageId = match.pane + QLatin1Char(':') + group.view;
                showPage(pageId);
            }
        }
    }
    if (SchemaSettingsPage *page = m_pages->page(pageId)) {
        page->revealRow(rowId, focusRow);
    }
}

// A subpage counts as its parent pane, which the sidebar keeps selected.
QString AppWindow::currentPane() const
{
    const SettingsSubpage *subpage = m_pages->schema().subpage(currentSubpage());
    return subpage ? subpage->parent : m_paneWidgets.key(m_stack->currentWidget());
}

QString AppWindow::currentSubpage() const
{
    return m_subpageWidgets.key(m_stack->currentWidget());
}

void AppWindow::selectPane(const QString &paneId)
{
    m_stack->setCurrentWidget(m_paneWidgets.value(paneId));
    // What's New enters and leaves the list as it is shown and left; any other
    // pick only moves the selection.
    if (sidebarListsWhatsNew() != m_sidebarListsWhatsNew) {
        rebuildSidebar();
        return;
    }
    const QSignalBlocker blocker(m_navigation);
    m_navigation->setCurrentItem(nullptr);
    for (int row = 0; row < m_navigation->count(); ++row) {
        if (m_navigation->item(row)->data(kPaneRole).toString() == paneId) {
            m_navigation->setCurrentRow(row);
        }
    }
}

bool AppWindow::sidebarListsWhatsNew() const
{
    return m_query.isEmpty()
        && (currentPane() == kWhatsNewPane || !m_controller->pendingWhatsNewVersion().isEmpty());
}

void AppWindow::showTranscribeFiles(const QStringList &paths)
{
    m_transcribe->addFiles(paths);
    showPage(QStringLiteral("transcribe"));
}

void AppWindow::refreshHeaderStripColor()
{
    // Every hairline (header divider, header underline, sidebar splitter
    // handle) uses the same color, derived from the same palette, so no line
    // reads lighter than its neighbors.
#ifdef Q_OS_MACOS
    const QPalette headerPalette = palette();
#else
    const QPalette headerPalette = settings::headerPalette(palette());
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
    // Fill the 1px splitter handle with a translucent hairline at the card
    // frame's contrast (a fifth of the text colour), not the opaque header
    // separator colour, which reads far brighter than every other separator.
    if (QWidget *handle = m_sidebarSplitter ? m_sidebarSplitter->handle(1) : nullptr) {
        QColor hairline = palette().color(QPalette::WindowText);
        hairline.setAlphaF(0.2F);
        QPalette handlePalette(handle->palette());
        handlePalette.setColor(QPalette::Window, hairline);
        handle->setPalette(handlePalette);
        handle->setAutoFillBackground(true);
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
    if (m_settingsDeletionStarted) return;
    if (!m_autoSaveTimer || !m_autoSaveTimer->isActive()) return;
    m_autoSaveTimer->stop();
    runAutoSave();
}

void AppWindow::closeEvent(QCloseEvent *event)
{
    if (!m_settingsDeletionStarted) {
        flushPendingAutoSave();
        rememberGeometry();
        warnThatClosingDoesNotQuitOnce();
    }
    QMainWindow::closeEvent(event);
}

// Closing the window never quits Speecher. A desktop with a tray shows the
// icon that says so; one without leaves a running process with an armed
// shortcut and nothing on screen, so say it once in words instead.
void AppWindow::warnThatClosingDoesNotQuitOnce()
{
    if (QApplication::quitOnLastWindowClosed() || QSystemTrayIcon::isSystemTrayAvailable()) {
        return;
    }
    QSettings &settings = m_controller->settings()->raw();
    const QString shownKey = QStringLiteral("ui/backgroundRunNoticeShown");
    if (settings.value(shownKey, false).toBool()) {
        return;
    }
    settings.setValue(shownKey, true);

    // Queued so the notice arrives after the window is off screen, which is
    // what it is talking about. A quit in flight ends the loop first and the
    // notice never appears, which is also what should happen.
    QTimer::singleShot(0, this, [this] {
        // Parented so the notice belongs to the window it is about, and lands
        // on that window's screen rather than wherever an ownerless dialog
        // goes.
        QMessageBox notice(this);
        notice.setIcon(QMessageBox::Information);
        notice.setWindowTitle(QStringLiteral("Speecher is still running"));
        notice.setText(QStringLiteral("Speecher keeps running in the background."));
        // The command and the app menu entry exist only where AppImage desktop
        // integration was installed, so neither is named here.
        notice.setInformativeText(QStringLiteral(
            "Your dictation shortcut still works. This desktop has no system tray, so start "
            "Speecher again to show this window."));
        QPushButton *quit = notice.addButton(QStringLiteral("Quit Speecher"),
                                             QMessageBox::DestructiveRole);
        notice.addButton(QStringLiteral("Keep running"), QMessageBox::AcceptRole);
        notice.exec();
        if (notice.clickedButton() == quit) {
            m_controller->quitApplication();
        }
    });
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

// One widget per pane, in the schema's order: Home and Transcribe are drawn
// here, every other pane is its SettingsPageSet page, and an Alternatives pane
// shows its views as tabs.
void AppWindow::buildPages()
{
    for (const SettingsPane &pane : m_pages->schema().panes) {
        QWidget *widget = nullptr;
        if (pane.layout == PaneLayout::Home) {
            widget = m_home;
        } else if (pane.layout == PaneLayout::Transcribe) {
            widget = m_transcribe;
        } else if (pane.layout == PaneLayout::Alternatives) {
            // The tab bar sits over the views' card column, not at the pane's
            // edge, so it lines up with every other page's cards.
            auto *content = new QWidget(this);
            auto *layout = new QVBoxLayout(content);
            layout->setContentsMargins(0, style()->pixelMetric(QStyle::PM_LayoutTopMargin), 0, 0);
            layout->setSpacing(0);
            auto *barHost = new QWidget(content);
            auto *barLayout = new QHBoxLayout(barHost);
            auto *tabs = new QTabBar(barHost);
            tabs->setDocumentMode(true);
            tabs->setExpanding(false);
            barLayout->addWidget(tabs);
            barLayout->addStretch();
            auto *views = new QStackedWidget(content);
            for (const SettingsPaneGroup &group : pane.groups) {
                // A tab reads & as a mnemonic marker ("Replacements & snippets").
                QString title = group.title;
                tabs->addTab(title.replace(QLatin1Char('&'), QStringLiteral("&&")));
                views->addWidget(m_pages->page(pane.id + QLatin1Char(':') + group.view));
            }
            connect(tabs, &QTabBar::currentChanged, views, &QStackedWidget::setCurrentIndex);
            new TabBarColumn(barHost, views);
            m_viewTabs.insert(pane.id, tabs);
            layout->addWidget(barHost);
            layout->addWidget(views, 1);
            widget = content;
        } else {
            widget = m_pages->page(pane.id);
        }
        m_paneWidgets.insert(pane.id, widget);
    }
    for (const SettingsSubpage &subpage : m_pages->schema().subpages) {
        m_subpageWidgets.insert(subpage.id, m_pages->page(subpage.id));
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
    search->setPlaceholderText(QStringLiteral("Search settings…"));
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
    // What's New and the subpages are not sidebar pages, so while one shows,
    // the header carries the way back to the page it was opened from, as
    // System Settings does for a page reached from another one.
    m_backButton = new QToolButton(headerRight);
    m_backButton->setObjectName(QStringLiteral("pageBack"));
    m_backButton->setText(QStringLiteral("Back"));
    const QIcon backIcon = QIcon::fromTheme(QStringLiteral("go-previous"));
    m_backButton->setIcon(backIcon);
    m_backButton->setToolButtonStyle(backIcon.isNull() ? Qt::ToolButtonTextOnly
                                                       : Qt::ToolButtonIconOnly);
    m_backButton->setToolTip(QStringLiteral("Back"));
    m_backButton->setAutoRaise(true);
    m_backButton->hide();
    connect(m_backButton, &QToolButton::clicked, this, &AppWindow::goBack);
    headerRightLayout->addWidget(m_backButton);
    m_pageTitle = settings::makePageTitle(paneTitle(kHomePane), headerRight);
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

    m_sidebarSplitter = new HairlineSplitter(Qt::Horizontal, central);
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
                                      settings::relatedSpacing());
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
    m_navigation->setItemDelegate(new WholeItemDelegate(m_navigation));
    sidebarLayout->addWidget(m_navigation, 1);
#ifdef Q_OS_LINUX
    sidebarLayout->addSpacing(settings::relatedSpacing());
    auto *quit = new QPushButton(QStringLiteral("Quit Speecher"), sidebar);
    quit->setObjectName(QStringLiteral("quitSpeecher"));
    connect(quit, &QPushButton::clicked, m_controller, &ApplicationController::quitApplication);
    sidebarLayout->addWidget(quit);
#endif
    m_stack = new QStackedWidget(m_sidebarSplitter);
    m_stack->setObjectName(QStringLiteral("appPageStack"));
    for (const SettingsPane &pane : m_pages->schema().panes) {
        m_stack->addWidget(m_paneWidgets.value(pane.id));
    }
    for (QWidget *subpage : std::as_const(m_subpageWidgets)) {
        m_stack->addWidget(subpage);
    }
    auto *right = new QWidget(m_sidebarSplitter);
    right->setBackgroundRole(QPalette::Window);
    right->setAutoFillBackground(true);
    right->setMinimumWidth(480);
    auto *rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    m_updateBanner = new InlineMessage(right);
    m_updateBanner->setObjectName(QStringLiteral("updateBanner"));
    m_updateBanner->setPosition(InlineMessage::Position::Header);
    m_updateBannerText = m_updateBanner->label();
    m_updateBannerText->setObjectName(QStringLiteral("updateBannerText"));
    m_updateProgress = new QProgressBar(m_updateBanner);
    m_updateProgress->setRange(0, 100);
    m_updateProgress->setTextVisible(true);
    m_updateProgress->setMaximumWidth(160);
    m_updateBanner->addAction(m_updateProgress);
    m_updateAction = new QPushButton(m_updateBanner);
    m_updateAction->setObjectName(QStringLiteral("updateAction"));
    m_updateBanner->addAction(m_updateAction);
    m_updateLater = new QPushButton(m_updateBanner);
    m_updateBanner->addAction(m_updateLater);
    m_updateDismiss = m_updateBanner->closeButton();
    m_updateDismiss->setObjectName(QStringLiteral("dismissUpdate"));
    // The controller decides when the banner goes; the button only reports.
    disconnect(m_updateDismiss, &QToolButton::clicked, m_updateBanner, nullptr);
    connect(m_updateAction, &QPushButton::clicked, this, [this] {
        if (m_showingWhatsNewBanner) {
            showWhatsNew();
        } else {
            m_controller->updateBanner()->runAction();
        }
    });
    connect(m_updateLater, &QPushButton::clicked,
            m_controller->updateBanner(), &UpdateBanner::later);
    connect(m_updateDismiss, &QToolButton::clicked, this, [this] {
        if (m_showingWhatsNewBanner) {
            m_controller->clearPendingWhatsNew();
        } else {
            m_controller->updateBanner()->dismiss();
        }
    });
    rightLayout->addWidget(m_updateBanner);
    refreshUpdateBanner();
    m_autoSaveWarning = new InlineMessage(right);
    m_autoSaveWarning->setObjectName(QStringLiteral("autoSaveWarning"));
    m_autoSaveWarning->setPosition(InlineMessage::Position::Header);
    m_autoSaveWarning->setType(InlineMessage::Type::Warning);
    m_autoSaveWarningText = m_autoSaveWarning->label();
    m_autoSaveWarning->hide();
    rightLayout->addWidget(m_autoSaveWarning);
    rightLayout->addWidget(m_stack, 1);
    m_sidebarSplitter->addWidget(sidebar);
    m_sidebarSplitter->addWidget(right);
    m_sidebarSplitter->setStretchFactor(0, 0);
    m_sidebarSplitter->setStretchFactor(1, 1);
    m_sidebarPane = sidebar;
    m_searchSection = searchContainer;
    sidebar->installEventFilter(this);
    connect(m_sidebarSplitter, &QSplitter::splitterMoved, searchContainer,
            [searchContainer, sidebar] { searchContainer->setFixedWidth(sidebar->width()); });
    root->addWidget(m_sidebarSplitter, 1);
    setCentralWidget(central);
    // Re-run now that the hairline widgets and the splitter handle exist; the
    // first call above only colored the strip itself.
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
    // Every pick goes through showPage, so choosing What's New in the list is
    // the same as any other way of opening it.
    // A search hit opens at the row that matched. Moving through the hits
    // only scrolls to it; a click or Enter also focuses its control.
    const auto openSearchHit = [this](QListWidgetItem *item, bool focusRow) {
        const QString pane = item ? item->data(kPaneRole).toString() : QString();
        if (pane.isEmpty() || m_query.isEmpty()) {
            return false;
        }
        for (const SearchMatch &match : m_pages->searchSettings(m_query)) {
            if (match.pane == pane) {
                showSearchMatch(match, focusRow);
                return true;
            }
        }
        return false;
    };
    connect(m_navigation, &QListWidget::currentItemChanged, this,
            [this, openSearchHit](QListWidgetItem *item) {
                if (openSearchHit(item, false)) {
                    return;
                }
                const QString pane = item ? item->data(kPaneRole).toString() : QString();
                if (!pane.isEmpty() && pane != currentPane()) {
                    showPage(pane);
                }
            });
    for (auto commit : {&QListWidget::itemClicked, &QListWidget::itemActivated}) {
        connect(m_navigation, commit, this,
                [openSearchHit](QListWidgetItem *item) { openSearchHit(item, true); });
    }
    connect(m_stack, &QStackedWidget::currentChanged, this, [this] {
        const QString pane = currentPane();
        const SettingsSubpage *subpage = m_pages->schema().subpage(currentSubpage());
        m_pageTitle->setText(subpage ? subpage->title : paneTitle(pane));
        m_backButton->setVisible(pane == kWhatsNewPane || subpage);
    });
    connect(m_controller, &ApplicationController::whatsNewChanged, this, [this] {
        if (sidebarListsWhatsNew() != m_sidebarListsWhatsNew) {
            rebuildSidebar();
        }
    });
    connect(search, &QLineEdit::textChanged, this, [this](const QString &query) {
        m_query = query;
        rebuildSidebar();
    });
    connect(search, &QLineEdit::returnPressed, this, [this] {
        const QList<SearchMatch> hits = m_pages->searchSettings(m_query);
        if (!hits.isEmpty()) {
            showSearchMatch(hits.first(), true);
        }
    });
    auto *find = new QShortcut(QKeySequence::Find, this);
    connect(find, &QShortcut::activated, search, [search] {
        search->setFocus(Qt::ShortcutFocusReason);
        search->selectAll();
    });
    rebuildSidebar();
    showPage(kHomePane);
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
    const UpdateBannerModel banner = m_controller->updateBanner()->model();
    m_showingWhatsNewBanner = !banner.visible && !m_controller->pendingWhatsNewVersion().isEmpty();
    if (m_showingWhatsNewBanner) {
        const WhatsNewBannerModel whatsNew = whatsNewBanner(m_controller->updates()->currentVersion());
        m_updateBanner->setType(InlineMessage::Type::Positive);
        m_updateBannerText->setText(whatsNew.text);
        m_updateProgress->hide();
        m_updateAction->setText(whatsNew.action);
        m_updateAction->setEnabled(true);
        m_updateAction->show();
        m_updateLater->hide();
        m_updateDismiss->setToolTip(whatsNew.dismiss);
        m_updateDismiss->setAccessibleName(whatsNew.dismiss);
        m_updateDismiss->show();
        m_updateBanner->show();
        return;
    }
    m_updateBanner->setVisible(banner.visible);
    if (!banner.visible) {
        return;
    }
    m_updateBanner->setType(banner.tone == UpdateBannerModel::Tone::Error ? InlineMessage::Type::Error
                            : banner.tone == UpdateBannerModel::Tone::Positive
                                ? InlineMessage::Type::Positive
                                : InlineMessage::Type::Information);
    m_updateBannerText->setText(banner.text);
    m_updateProgress->setVisible(banner.progress >= 0);
    m_updateProgress->setValue(std::max(banner.progress, 0));
    m_updateAction->setText(banner.action);
    m_updateAction->setVisible(!banner.action.isEmpty());
    m_updateAction->setEnabled(banner.actionEnabled);
    m_updateLater->setText(banner.later);
    m_updateLater->setVisible(!banner.later.isEmpty());
    m_updateDismiss->setToolTip(banner.dismiss);
    m_updateDismiss->setAccessibleName(banner.dismiss);
    m_updateDismiss->setVisible(!banner.dismiss.isEmpty());
}

void AppWindow::showWhatsNew()
{
    if (currentPane() != kWhatsNewPane) {
        m_whatsNewReturnPane = currentPane();
    }
    m_controller->clearPendingWhatsNew();
    selectPane(kWhatsNewPane);
}

void AppWindow::goBack()
{
    if (const SettingsSubpage *subpage = m_pages->schema().subpage(currentSubpage())) {
        showPage(subpage->parent);
        return;
    }
    selectPane(m_whatsNewReturnPane.isEmpty() ? kHomePane : m_whatsNewReturnPane);
}

// Where a pane item's content starts, its icon, as the style lays out an item
// of the sidebar; System Settings starts its section titles there.
static int sidebarContentInset(const QListWidget *navigation)
{
    QStyleOptionViewItem option;
    option.initFrom(navigation);
    option.features = QStyleOptionViewItem::HasDisplay | QStyleOptionViewItem::HasDecoration;
    option.decorationSize = navigation->iconSize();
    option.decorationPosition = QStyleOptionViewItem::Left;
    option.displayAlignment = Qt::AlignLeft | Qt::AlignVCenter;
    option.text = QStringLiteral("M");
    option.rect = QRect(0, 0, navigation->viewport()->width(), 32);
    return navigation->style()->subElementRect(QStyle::SE_ItemViewItemDecoration, &option, navigation).left();
}

// The panes in their groups, each titled group under a header, and What's
// New first in the top group while it is pending or showing. A search lists
// its hits alone.
void AppWindow::rebuildSidebar()
{
    const QSignalBlocker blocker(m_navigation);
    m_navigation->clear();
    const QString current = currentPane();
    const SettingsSchema &schema = m_pages->schema();
    const auto addPane = [this, &schema, &current](const QString &id) {
        const SettingsPane *pane = schema.pane(id);
        auto *item = new QListWidgetItem(paneIcon(pane->iconId), pane->title, m_navigation);
        item->setData(kPaneRole, id);
        item->setSizeHint(QSize(0, 32));
        if (id == current) {
            m_navigation->setCurrentItem(item);
        }
    };
    // Kirigami's ListSectionHeader, as System Settings' sidebar has it: the
    // group's title in the section bold where the items' icons start, then a
    // line the style draws running to the row's right edge. The row is an
    // item nothing can select or land on with the keyboard.
    const int inset = sidebarContentInset(m_navigation);
    const auto addHeader = [this, inset](const QString &title) {
        auto *item = new QListWidgetItem(m_navigation);
        item->setFlags(Qt::NoItemFlags);
        auto *header = new QWidget(m_navigation);
        header->setObjectName(QStringLiteral("sidebarHeader"));
        header->setFocusPolicy(Qt::NoFocus);
        header->setAttribute(Qt::WA_TransparentForMouseEvents);
        auto *layout = new QHBoxLayout(header);
        layout->setContentsMargins(inset, settings::relatedSpacing(), 0, 0);
        layout->setSpacing(settings::relatedSpacing());
        auto *label = new QLabel(title, header);
        label->setObjectName(QStringLiteral("sidebarHeaderTitle"));
        label->setFont(settings::sectionTitleFont(label->font()));
        layout->addWidget(label);
        // Breeze draws this line darker than Kirigami's Separator (a fifth of
        // the text over the background), so it reads at least as clearly.
        auto *line = new QFrame(header);
        line->setObjectName(QStringLiteral("sidebarHeaderLine"));
        line->setFrameShape(QFrame::HLine);
        layout->addWidget(line, 1, Qt::AlignVCenter);
        item->setSizeHint(header->sizeHint());
        m_navigation->setItemWidget(item, header);
    };
    m_sidebarListsWhatsNew = sidebarListsWhatsNew();
    if (!m_query.isEmpty()) {
        const QList<SearchMatch> hits = m_pages->searchSettings(m_query);
        for (const SearchMatch &hit : hits) {
            addPane(hit.pane);
        }
        if (hits.isEmpty()) {
            auto *none = new QListWidgetItem(noSettingsMatchText(), m_navigation);
            none->setFlags(Qt::NoItemFlags);
            none->setSizeHint(QSize(0, 32));
        }
        return;
    }
    if (m_sidebarListsWhatsNew) {
        addPane(kWhatsNewPane);
    }
    for (const SidebarGroup &group : schema.sidebarGroups) {
        if (!group.title.isEmpty()) {
            addHeader(group.title);
        }
        for (const QString &id : group.panes) {
            addPane(id);
        }
    }
}

void AppWindow::showHome()
{
    showPage(kHomePane);
}

void AppWindow::runAutoSave()
{
    if (m_settingsDeletionStarted) return;
    SettingsPageSet::SaveOutcome outcome;
    const bool saved = m_pages->save(false, false, &outcome);
    if (!saved) {
        m_autoSaveWarningText->setText(outcome.messages.join(QLatin1Char('\n')));
    }
    m_autoSaveWarning->setVisible(!saved);
    // The Insights setting and the learned corrections both show on Home.
    m_home->refresh();
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
