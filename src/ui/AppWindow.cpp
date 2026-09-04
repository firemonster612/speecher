#include "ui/AppWindow.h"

#include "app/ApplicationController.h"
#include "app/UpdateController.h"
#include "core/SettingsStore.h"
#include "frontend/qt/SchemaSettingsPage.h"
#include "ui/DictationPage.h"
#include "ui/settings/FormCard.h"
#include "ui/settings/SettingsPageSet.h"
#include "ui/settings/SettingsPageSupport.h"

#include <QCloseEvent>
#include <QAbstractItemDelegate>
#include <QAbstractItemView>
#include <QEvent>
#include <QApplication>
#include <QCheckBox>
#include <QDebug>
#include <QFileSystemWatcher>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMouseEvent>
#include <QWindow>
#include <QLineEdit>
#include <QListWidget>
#include <QListView>
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
#include <QStandardPaths>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QTextDocument>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <utility>

#ifdef SPEECHER_WITH_KPAGEWIDGET
#include <KPageWidget>
#include <KPageWidgetItem>
#endif

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

// The sidebar, in order: Dictation, then the schema's pages as AppPageId
// numbers them.
const QList<PageDefinition> kPages{
    {QStringLiteral("Dictation"), QStringLiteral("audio-input-microphone"), QString()},
    {QStringLiteral("General"), QStringLiteral("preferences-system"), QString()},
    {QStringLiteral("Audio"), QStringLiteral("preferences-desktop-sound"), QString()},
    {QStringLiteral("Output"), QStringLiteral("edit-paste"), QStringLiteral("edit-copy")},
    {QStringLiteral("Accounts"), QStringLiteral("preferences-desktop-user-password"), QStringLiteral("dialog-password")},
    {QStringLiteral("Refinement"), QStringLiteral("tools-wizard"), QStringLiteral("document-edit")},
    {QStringLiteral("Vocabulary"), QStringLiteral("accessories-dictionary"), QStringLiteral("tools-check-spelling")},
};

constexpr int kSidebarMinimumWidth = 180;
constexpr int kSidebarDefaultWidth = 220;
constexpr int kSidebarMaximumWidth = 320;

// The words on a page that a search can match: its rows, and anything else
// with text on it, such as the Dictation page's summary cards.
QString pageSearchText(const QString &title, QWidget *page)
{
    const auto cleanText = [](QString text) {
        text = text.trimmed();
        while (text.endsWith(QLatin1Char(':')) || text.endsWith(QChar(0x2026))) {
            text.chop(1);
            text = text.trimmed();
        }
        return text;
    };
    QStringList keywords{title};
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
    for (QPushButton *button : page->findChildren<QPushButton *>()) {
        if (!button->isHidden()) {
            const QString text = cleanText(button->text());
            if (!text.isEmpty()) keywords.append(text);
        }
    }
    for (settings::FormRow *row : page->findChildren<settings::FormRow *>()) {
        if (row->isVisibleTo(page)) {
            keywords.append(row->searchText());
        }
    }
    return keywords.join(QLatin1Char('\n'));
}

// A theme without the icon leaves the row text-only: a stand-in document icon
// would say every page is a file.
QIcon pageIcon(const PageDefinition &page)
{
    return QIcon::fromTheme(page.iconName, QIcon::fromTheme(page.fallbackIconName));
}

class ViewItemPositionDelegate final : public QAbstractItemDelegate {
public:
    ViewItemPositionDelegate(QAbstractItemDelegate *delegate, QObject *parent)
        : QAbstractItemDelegate(parent)
        , m_delegate(delegate)
    {
        setObjectName(QStringLiteral("viewItemPositionSidebarDelegate"));
    }

    void paint(QPainter *painter,
               const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        QStyleOptionViewItem roundedOption(option);
        roundedOption.viewItemPosition = QStyleOptionViewItem::OnlyOne;
#ifdef SPEECHER_BUNDLED_BREEZE_NEEDS_VIEW_ITEM_POSITION_COMPAT
        const QStyle *style = roundedOption.widget ? roundedOption.widget->style()
                                                   : QApplication::style();
        // Breeze commit aba0f922 added rounded viewItemPosition painting in
        // 6.6.90. Older bundled releases need the same shape for hover and
        // selection; every other host-selected style still paints itself.
        if ((roundedOption.state & (QStyle::State_Selected | QStyle::State_MouseOver))
            && style->objectName().compare(QStringLiteral("breeze"), Qt::CaseInsensitive) == 0) {
            const QPalette::ColorGroup group =
                !(roundedOption.state & QStyle::State_Enabled)
                ? QPalette::Disabled
                : (roundedOption.state & QStyle::State_Active) ? QPalette::Active
                                                               : QPalette::Inactive;
            QColor highlight = roundedOption.palette.color(group, QPalette::Highlight);
            if (roundedOption.state & QStyle::State_MouseOver) {
                if (roundedOption.state & QStyle::State_Selected) {
                    highlight = highlight.lighter(110);
                } else {
                    highlight.setAlphaF(0.2);
                }
            }
            const int frameWidth = style->pixelMetric(
                QStyle::PM_DefaultFrameWidth, &roundedOption, roundedOption.widget);
            const int focusMargin = style->pixelMetric(
                QStyle::PM_FocusFrameHMargin, &roundedOption, roundedOption.widget);
            const int radius = qMax(frameWidth, focusMargin * 2);
            const int horizontalInset = radius;
            const int verticalInset = qMax(0, frameWidth / 2);
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);
            painter->setPen(Qt::NoPen);
            painter->setBrush(highlight);
            painter->drawRoundedRect(
                roundedOption.rect.adjusted(horizontalInset,
                                            verticalInset,
                                            -horizontalInset,
                                            -verticalInset),
                radius,
                radius);
            painter->restore();
            for (const auto colorGroup : {QPalette::Active,
                                          QPalette::Inactive,
                                          QPalette::Disabled}) {
                roundedOption.palette.setColor(colorGroup, QPalette::Highlight, Qt::transparent);
            }
        }
#endif
        m_delegate->paint(painter, roundedOption, index);
    }

    QSize sizeHint(const QStyleOptionViewItem &option,
                   const QModelIndex &index) const override
    {
        return m_delegate->sizeHint(option, index);
    }

private:
    QAbstractItemDelegate *m_delegate;
};

class FallbackSidebarItemDelegate final : public QStyledItemDelegate {
public:
    explicit FallbackSidebarItemDelegate(QObject *parent)
        : QStyledItemDelegate(parent)
    {
        setObjectName(QStringLiteral("viewItemPositionSidebarDelegate"));
    }

    void paint(QPainter *painter,
               const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        QStyleOptionViewItem roundedOption(option);
        roundedOption.viewItemPosition = QStyleOptionViewItem::OnlyOne;
        QStyledItemDelegate::paint(painter, roundedOption, index);
    }
};

#ifdef SPEECHER_WITH_KPAGEWIDGET
class NativePageWidget final : public KPageWidget {
public:
    explicit NativePageWidget(QWidget *parent)
        : KPageWidget(parent)
    {
    }

    QListView *navigationView() const { return m_navigationView; }

protected:
    QAbstractItemView *createView() override
    {
        QAbstractItemView *view = KPageWidget::createView();
        m_navigationView = qobject_cast<QListView *>(view);
        return view;
    }

private:
    QListView *m_navigationView = nullptr;
};
#endif

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
    connect(m_pages, &SettingsPageSet::settingsDeletionStarted, this, [this] {
        m_settingsDeletionStarted = true;
        m_autoSaveTimer->stop();
        m_autoSaveWarning->hide();
    });

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
    showPage(static_cast<int>(page) + 1);
}

bool AppWindow::navigateToSettingsPage(const QString &name)
{
    const std::optional<AppPageId> page = appPageFromName(name);
    navigateToSettings(page.value_or(AppPageId::General));
    return page.has_value();
}

void AppWindow::showPage(int index)
{
#ifdef SPEECHER_WITH_KPAGEWIDGET
    m_navigation->setCurrentPage(m_navigationPages.at(index));
#else
    for (int row = 0; row < m_navigation->count(); ++row) {
        QListWidgetItem *item = m_navigation->item(row);
        if (item->data(Qt::UserRole).toInt() == index) {
            m_navigation->setCurrentItem(item);
            break;
        }
    }
#endif
}

// Shows the page the row is on, scrolls it into view and points at it.
void AppWindow::jumpToRow(settings::FormRow *row)
{
    for (int index = 0; index < m_pageWidgets.size(); ++index) {
        if (m_pageWidgets.at(index)->isAncestorOf(row)) {
            showPage(index);
            break;
        }
    }
    // The page has just been switched to, so its layout settles a turn later.
    QTimer::singleShot(0, row, [row] {
        for (QWidget *ancestor = row->parentWidget(); ancestor; ancestor = ancestor->parentWidget()) {
            if (auto *scroll = qobject_cast<QScrollArea *>(ancestor)) {
                scroll->ensureWidgetVisible(row, 0, settings::sectionGap());
                break;
            }
        }
        row->flash();
    });
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
    if (m_searchSection) {
        m_searchSection->setPalette(headerPalette);
    }
    for (QWidget *hairline : {m_headerDividerLine, m_headerUnderline}) {
        if (hairline) {
            QPalette linePalette(hairline->palette());
            linePalette.setColor(QPalette::Window, line);
            hairline->setPalette(linePalette);
        }
    }
    // Fill the 1px splitter handle: Breeze paints handles in plain window
    // colour, which shows as a see-through seam between sidebar and content.
    if (QWidget *handle = m_sidebarSplitter ? m_sidebarSplitter->handle(1) : nullptr) {
        QPalette handlePalette(handle->palette());
        handlePalette.setColor(QPalette::Window, line);
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
    }
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
    if (watched == m_headerStrip || watched == m_searchSection) {
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
    m_pages->preserveBindingScroll(m_pages->vocabulary());
    m_pageWidgets = {
        m_dictation,
        m_pages->general(),
        m_pages->audio(),
        m_pages->output(),
        m_pages->accounts(),
        m_pages->refinement(),
        m_pages->vocabulary(),
        m_pages->whatsNew(),
    };
}

void AppWindow::buildStatusBanners(QWidget *parent, QVBoxLayout *layout)
{
    m_updateBanner = new QFrame(parent);
    m_updateBanner->setObjectName(QStringLiteral("updateBanner"));
    m_updateBanner->setFrameShape(QFrame::StyledPanel);
    auto *updateLayout = new QHBoxLayout(m_updateBanner);
    updateLayout->setContentsMargins(settings::relatedSpacing(),
                                     settings::tightSpacing(),
                                     settings::relatedSpacing(),
                                     settings::tightSpacing());
    m_updateBannerText = new QLabel(m_updateBanner);
    m_updateBannerText->setObjectName(QStringLiteral("updateBannerText"));
    updateLayout->addWidget(m_updateBannerText, 1);
    m_updateProgress = new QProgressBar(m_updateBanner);
    m_updateProgress->setRange(0, 100);
    m_updateProgress->setTextVisible(true);
    m_updateProgress->setMaximumWidth(160);
    updateLayout->addWidget(m_updateProgress);
    m_updateAction = new QPushButton(m_updateBanner);
    m_updateAction->setObjectName(QStringLiteral("updateAction"));
    updateLayout->addWidget(m_updateAction);
    m_updateLater = new QPushButton(QStringLiteral("Later"), m_updateBanner);
    updateLayout->addWidget(m_updateLater);
    m_updateDismiss = new QToolButton(m_updateBanner);
    m_updateDismiss->setObjectName(QStringLiteral("dismissUpdate"));
    m_updateDismiss->setIcon(QIcon::fromTheme(
        QStringLiteral("window-close"),
        style()->standardIcon(QStyle::SP_TitleBarCloseButton)));
    m_updateDismiss->setToolTip(QStringLiteral("Dismiss"));
    m_updateDismiss->setAccessibleName(QStringLiteral("Dismiss update"));
    m_updateDismiss->setAutoRaise(true);
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
    connect(m_updateDismiss, &QToolButton::clicked, this, [this] {
        if (m_showingWhatsNewBanner) {
            m_controller->clearPendingWhatsNew();
        } else {
            m_controller->updates()->dismissAvailableVersion();
        }
    });
    layout->addWidget(m_updateBanner);
    refreshUpdateBanner();

    m_autoSaveWarning = new QFrame(parent);
    m_autoSaveWarning->setObjectName(QStringLiteral("autoSaveWarning"));
    m_autoSaveWarning->setFrameShape(QFrame::StyledPanel);
    auto *warningLayout = new QHBoxLayout(m_autoSaveWarning);
    m_autoSaveWarningText = new QLabel(m_autoSaveWarning);
    warningLayout->addWidget(m_autoSaveWarningText);
    m_autoSaveWarning->hide();
    layout->addWidget(m_autoSaveWarning);
}

void AppWindow::buildHeaderTitle(QWidget *parent, QHBoxLayout *layout)
{
    m_backButton = new QToolButton(parent);
    m_backButton->setObjectName(QStringLiteral("whatsNewBack"));
    m_backButton->setText(QStringLiteral("Back"));
    const QIcon backIcon = QIcon::fromTheme(QStringLiteral("go-previous"));
    m_backButton->setIcon(backIcon);
    m_backButton->setToolButtonStyle(backIcon.isNull() ? Qt::ToolButtonTextOnly
                                                       : Qt::ToolButtonIconOnly);
    m_backButton->setToolTip(QStringLiteral("Back"));
    m_backButton->setAutoRaise(true);
    m_backButton->hide();
    connect(m_backButton, &QToolButton::clicked, this, &AppWindow::leaveWhatsNew);
    layout->addWidget(m_backButton);
    m_pageTitle = settings::makePageTitle(kPages.first().title, parent);
    layout->addWidget(m_pageTitle);
    layout->addStretch();
}

void AppWindow::watchHeaderColorConfig()
{
#ifndef Q_OS_MACOS
    auto *watcher = new QFileSystemWatcher(this);
    const QString kdeGlobals =
        QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/kdeglobals");
    watcher->addPath(kdeGlobals);
    connect(watcher, &QFileSystemWatcher::fileChanged, this, [this, watcher](const QString &path) {
        watcher->addPath(path);
        QTimer::singleShot(0, this, &AppWindow::refreshHeaderStripColor);
    });
#endif
}

#ifdef SPEECHER_WITH_KPAGEWIDGET
void AppWindow::buildNativeSidebarShell()
{
    resize(900, 640);
    setMinimumSize(760, 520);

    auto *nativeNavigation = new NativePageWidget(this);
    m_navigation = nativeNavigation;
    m_navigation->setObjectName(QStringLiteral("appNavigation"));
    m_navigation->setFaceType(KPageView::FlatList);
    m_navigation->setBackgroundRole(QPalette::Window);
    m_navigation->setAutoFillBackground(true);
    setCentralWidget(m_navigation);

    for (int index = 0; index < m_pageWidgets.size(); ++index) {
        const bool whatsNew = index >= kPages.size();
        auto *item = m_navigation->addPage(
            m_pageWidgets.at(index),
            whatsNew ? QStringLiteral("What's New") : kPages.at(index).title);
        item->setHeaderVisible(false);
        if (!whatsNew) {
            item->setIcon(pageIcon(kPages.at(index)));
        }
        m_navigationPages.append(item);
    }

    m_navigationView = nativeNavigation->navigationView();
    if (m_navigationView) {
        m_navigationView->setObjectName(QStringLiteral("appNavigationView"));
        m_navigationView->setIconSize(QSize(22, 22));
        m_navigationView->setRowHidden(kPages.size(), true);
        m_navigationView->setBackgroundRole(QPalette::Base);
        m_navigationView->setAutoFillBackground(true);
        m_navigationView->viewport()->setBackgroundRole(QPalette::Base);
        m_navigationView->viewport()->setAutoFillBackground(true);
    } else {
        qWarning().noquote()
            << "KPageView FlatList navigation view was not found; sidebar view customization was skipped";
    }
    if (QAbstractItemDelegate *nativeDelegate = m_navigation->itemDelegate()) {
        m_navigation->setItemDelegate(
            new ViewItemPositionDelegate(nativeDelegate, m_navigation));
    } else {
        qWarning().noquote()
            << "KPageView item delegate was not found; rounded selection hint was skipped";
    }

    m_searchSection = m_navigation->findChild<QWidget *>(QStringLiteral("KPageView::Search"));
    QLineEdit *search = nullptr;
    if (m_searchSection) {
        m_searchSection->setObjectName(QStringLiteral("sidebarSearchContainer"));
        m_searchSection->setBackgroundRole(QPalette::Window);
        m_searchSection->setAutoFillBackground(true);
        search = m_searchSection->findChild<QLineEdit *>();
        if (search) {
            // KPageView's delayed filter would unhide the permanently hidden
            // What's New Page after Clear. Replace only that signal's wiring.
            QObject::disconnect(search, SIGNAL(textChanged(QString)), nullptr, nullptr);
            search->setObjectName(QStringLiteral("appSearch"));
        } else {
            qWarning().noquote()
                << "KPageView search field was not found; sidebar search wiring was skipped";
        }
    } else {
        qWarning().noquote()
            << "KPageView search container was not found; sidebar search wiring was skipped";
    }

    auto *pageHeader = new QWidget(m_navigation);
    auto *pageHeaderLayout = new QVBoxLayout(pageHeader);
    pageHeaderLayout->setContentsMargins(0, 0, 0, 0);
    pageHeaderLayout->setSpacing(0);
    auto *header = new QWidget(pageHeader);
    header->setObjectName(QStringLiteral("sidebarHeaderStrip"));
    header->setBackgroundRole(QPalette::Window);
    header->setAutoFillBackground(true);
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(0);

    auto *headerDivider = new QWidget(header);
    auto *dividerLayout = new QVBoxLayout(headerDivider);
    dividerLayout->setContentsMargins(0, settings::relatedSpacing(), 0, settings::relatedSpacing());
    m_headerDividerLine = new QWidget(headerDivider);
    m_headerDividerLine->setFixedWidth(1);
    m_headerDividerLine->setAutoFillBackground(true);
    dividerLayout->addWidget(m_headerDividerLine);
    headerLayout->addWidget(headerDivider);

    auto *headerContent = new QWidget(header);
    auto *headerContentLayout = new QVBoxLayout(headerContent);
    headerContentLayout->setContentsMargins(settings::relatedSpacing(),
                                            settings::relatedSpacing(),
                                            settings::relatedSpacing(),
                                            settings::relatedSpacing());
    headerContentLayout->setSpacing(settings::tightSpacing());
    auto *titleLayout = new QHBoxLayout;
    titleLayout->setContentsMargins(0, 0, 0, 0);
    buildHeaderTitle(headerContent, titleLayout);
    headerContentLayout->addLayout(titleLayout);
    headerLayout->addWidget(headerContent, 1);
    m_headerStrip = header;
    header->installEventFilter(this);
    pageHeaderLayout->addWidget(header);
    // The filled widgets use one shared palette color, so the horizontal and
    // vertical hairlines meet without QFrame applying a second style tint.
    m_headerUnderline = new QWidget(pageHeader);
    m_headerUnderline->setFixedHeight(1);
    m_headerUnderline->setAutoFillBackground(true);
    pageHeaderLayout->addWidget(m_headerUnderline);
    buildStatusBanners(pageHeader, pageHeaderLayout);
    m_navigation->setPageHeader(pageHeader);

    QFrame *nativeHeaderSeparator = nullptr;
    for (QFrame *frame : m_navigation->findChildren<QFrame *>(
             QString(), Qt::FindDirectChildrenOnly)) {
        if (frame->frameShape() == QFrame::HLine) {
            nativeHeaderSeparator = frame;
            break;
        }
    }
    if (nativeHeaderSeparator) {
        nativeHeaderSeparator->hide();
    } else {
        qWarning().noquote()
            << "KPageView header separator was not found; native separator hiding was skipped";
    }

#ifdef Q_OS_LINUX
    auto *footer = new QWidget(m_navigation);
    auto *footerLayout = new QVBoxLayout(footer);
    footerLayout->setContentsMargins(settings::relatedSpacing(),
                                     settings::relatedSpacing(),
                                     settings::relatedSpacing(),
                                     settings::relatedSpacing());
    auto *quit = new QPushButton(QStringLiteral("Quit Speecher"), footer);
    quit->setObjectName(QStringLiteral("quitSpeecher"));
    connect(quit, &QPushButton::clicked, m_controller, &ApplicationController::quitApplication);
    footerLayout->addWidget(quit);
    m_navigation->setPageFooter(footer);
#endif

    watchHeaderColorConfig();

    m_sidebarPane = m_navigationView;
    if (m_sidebarPane) {
        m_sidebarPane->installEventFilter(this);
    }
    if (m_searchSection) {
        m_searchSection->installEventFilter(this);
    }
    QTimer::singleShot(0, this, [this] {
        if (m_searchSection && m_sidebarPane) {
            m_searchSection->setFixedWidth(m_sidebarPane->width());
        }
    });
    refreshHeaderStripColor();

    connect(m_navigation,
            &KPageWidget::currentPageChanged,
            this,
            [this](KPageWidgetItem *item) {
                const int index = m_navigationPages.indexOf(item);
                if (index < 0) {
                    return;
                }
                const bool whatsNew = index >= kPages.size();
                m_pageTitle->setText(whatsNew ? QStringLiteral("What's New")
                                              : kPages.at(index).title);
                m_backButton->setVisible(whatsNew);
            });
    if (search && m_navigationView) {
        connect(search, &QLineEdit::textChanged, this, &AppWindow::filterSidebarPages);
        connect(search, &QLineEdit::returnPressed, this, &AppWindow::openSearchResult);
        auto *clearSearch = new QShortcut(QKeySequence(Qt::Key_Escape), search);
        clearSearch->setContext(Qt::WidgetShortcut);
        connect(clearSearch, &QShortcut::activated, search, &QLineEdit::clear);
    }
    m_navigation->setCurrentPage(m_navigationPages.first());

    m_autoSaveTimer = new QTimer(this);
    m_autoSaveTimer->setSingleShot(true);
    m_autoSaveTimer->setInterval(600);
    connect(m_pages, &SettingsPageSet::changed, m_autoSaveTimer, qOverload<>(&QTimer::start));
    connect(m_autoSaveTimer, &QTimer::timeout, this, &AppWindow::runAutoSave);
}
#endif

void AppWindow::buildSidebarShell()
{
#ifdef SPEECHER_WITH_KPAGEWIDGET
    buildNativeSidebarShell();
    return;
#else
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
    // What's New is not a sidebar page, so while it shows, the header carries
    // the way back to the page it was opened from, as System Settings does for
    // a page reached from another one.
    buildHeaderTitle(headerRight, headerRightLayout);
    headerLayout->addWidget(headerRight, 1);
    header->installEventFilter(this);
    root->addWidget(header);

    watchHeaderColorConfig();

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
    sidebar->setMinimumWidth(kSidebarMinimumWidth);
    sidebar->setMaximumWidth(kSidebarMaximumWidth);
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
    m_navigation->setItemDelegate(new FallbackSidebarItemDelegate(m_navigation));
    m_navigation->setSpacing(2);
    m_navigation->setIconSize(QSize(22, 22));
    for (int index = 0; index < kPages.size(); ++index) {
        const auto &page = kPages.at(index);
        auto *item = new QListWidgetItem(pageIcon(page), page.title, m_navigation);
        item->setData(Qt::UserRole, index);
        item->setSizeHint(QSize(0, 32));
    }
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
    for (QWidget *page : std::as_const(m_pageWidgets)) {
        m_stack->addWidget(page);
    }
    auto *right = new QWidget(m_sidebarSplitter);
    right->setBackgroundRole(QPalette::Window);
    right->setAutoFillBackground(true);
    right->setMinimumWidth(480);
    auto *rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    buildStatusBanners(right, rightLayout);
    rightLayout->addWidget(m_stack, 1);
    m_sidebarSplitter->addWidget(sidebar);
    m_sidebarSplitter->addWidget(right);
    m_sidebarSplitter->setStretchFactor(0, 0);
    m_sidebarSplitter->setStretchFactor(1, 1);
    m_sidebarPane = sidebar;
    m_searchSection = searchContainer;
    sidebar->installEventFilter(this);
    searchContainer->installEventFilter(this);
    connect(m_sidebarSplitter, &QSplitter::splitterMoved, searchContainer,
            [searchContainer, sidebar] { searchContainer->setFixedWidth(sidebar->width()); });
    root->addWidget(m_sidebarSplitter, 1);
    setCentralWidget(central);
    // Re-run now that the hairline widgets and the splitter handle exist; the
    // first call above only colored the strip itself.
    refreshHeaderStripColor();
    m_sidebarSplitter->setSizes({kSidebarDefaultWidth, 680});
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
        if (index < 0) {
            return;
        }
        const bool whatsNew = index >= kPages.size();
        m_pageTitle->setText(whatsNew ? QStringLiteral("What's New") : kPages.at(index).title);
        m_backButton->setVisible(whatsNew);
    });
    connect(search, &QLineEdit::textChanged, this, &AppWindow::filterSidebarPages);
    connect(search, &QLineEdit::returnPressed, this, &AppWindow::openSearchResult);
    auto *clearSearch = new QShortcut(QKeySequence(Qt::Key_Escape), search);
    clearSearch->setContext(Qt::WidgetShortcut);
    connect(clearSearch, &QShortcut::activated, search, &QLineEdit::clear);

    m_autoSaveTimer = new QTimer(this);
    m_autoSaveTimer->setSingleShot(true);
    m_autoSaveTimer->setInterval(600);
    connect(m_pages, &SettingsPageSet::changed, m_autoSaveTimer, qOverload<>(&QTimer::start));
    connect(m_autoSaveTimer, &QTimer::timeout, this, &AppWindow::runAutoSave);
#endif
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
    // "Later" hides states where restart is not yet underway.
    // Once restarting has begun, keep its status visible to explain the exit.
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
        m_updateBannerText->setText(updates->stableReplacementAvailable()
                                        ? QStringLiteral("Switch to Stable Release %1 (replaces "
                                                         "this Nightly Build)")
                                              .arg(updates->availableVersion())
                                        : QStringLiteral("Speecher %1 is available")
                                              .arg(updates->availableVersion()));
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
        // The sentence is the whole message; a disabled button repeating it
        // would only add a control that cannot be used.
        m_updateBannerText->setText(QStringLiteral("Restarting after this dictation…"));
        m_updateAction->hide();
        break;
    case UpdateController::State::Restarting:
        m_updateBannerText->setText(QStringLiteral("Restarting…"));
        m_updateAction->hide();
        break;
    case UpdateController::State::Error:
        m_updateBannerText->setText(updates->errorMessage());
        m_updateAction->setText(updates->manualInstallRequired()
                                    ? QStringLiteral("Open release page")
                                    : QStringLiteral("Try again"));
        m_updateDismiss->show();
        break;
    default:
        m_updateBanner->hide();
        break;
    }
}

void AppWindow::showWhatsNew()
{
#ifdef SPEECHER_WITH_KPAGEWIDGET
    const int currentRow = m_navigationPages.indexOf(m_navigation->currentPage());
    if (currentRow >= 0 && currentRow < kPages.size()) {
        m_whatsNewReturnRow = currentRow;
    }
    m_navigation->setCurrentPage(m_navigationPages.last());
#else
    if (m_navigation->currentRow() >= 0) {
        m_whatsNewReturnRow = m_navigation->currentRow();
    }
    m_navigation->setCurrentItem(nullptr);
    m_stack->setCurrentWidget(m_pages->whatsNew());
#endif
    m_controller->clearPendingWhatsNew();
}

void AppWindow::leaveWhatsNew()
{
#ifdef SPEECHER_WITH_KPAGEWIDGET
    m_navigation->setCurrentPage(
        m_navigationPages.at(qBound(0, m_whatsNewReturnRow, kPages.size() - 1)));
#else
    m_navigation->setCurrentRow(qBound(0, m_whatsNewReturnRow, m_navigation->count() - 1));
#endif
}

// Hides the sidebar pages the query matches nothing on, and remembers the
// rows it does match. One matching row is unambiguous, so the search goes
// straight to it.
void AppWindow::filterSidebarPages(const QString &query)
{
#ifdef SPEECHER_WITH_KPAGEWIDGET
    if (!m_navigationView) {
        return;
    }
#endif
    m_rowMatches.clear();
    QList<bool> hidden(kPages.size(), false);
    if (!query.isEmpty()) {
        for (int pageIndex = 0; pageIndex < kPages.size(); ++pageIndex) {
            QWidget *page = m_pageWidgets.at(pageIndex);
            hidden[pageIndex] = !pageSearchText(kPages.at(pageIndex).title, page)
                                     .contains(query, Qt::CaseInsensitive);
            for (settings::FormRow *row : page->findChildren<settings::FormRow *>()) {
                if (row->isVisibleTo(page) && row->objectName() != QStringLiteral("gateNote")
                    && row->searchText().contains(query, Qt::CaseInsensitive)) {
                    m_rowMatches.append(row);
                }
            }
        }
    }

#ifdef SPEECHER_WITH_KPAGEWIDGET
    for (int row = 0; row < kPages.size(); ++row) {
        m_navigationView->setRowHidden(row, hidden.at(row));
    }
    m_navigationView->setRowHidden(kPages.size(), true);
#else
    for (int row = 0; row < m_navigation->count(); ++row) {
        m_navigation->item(row)->setHidden(hidden.at(row));
    }
#endif
    if (m_rowMatches.size() == 1) {
        jumpToRow(m_rowMatches.first());
    }
}

// Enter in the search field: the first matching row, or failing that the
// first page still shown.
void AppWindow::openSearchResult()
{
    if (!m_rowMatches.isEmpty()) {
        jumpToRow(m_rowMatches.first());
        return;
    }
    for (int row = 0; row < kPages.size(); ++row) {
#ifdef SPEECHER_WITH_KPAGEWIDGET
        const bool shown = !m_navigationView->isRowHidden(row);
#else
        const bool shown = !m_navigation->item(row)->isHidden();
#endif
        if (shown) {
            showPage(row);
            return;
        }
    }
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
