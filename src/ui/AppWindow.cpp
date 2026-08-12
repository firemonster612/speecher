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

#include <QCloseEvent>
#include <QEvent>
#include <QCheckBox>
#include <QFileSystemWatcher>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPalette>
#include <QPushButton>
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

namespace speecher {

namespace {

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
    scroll->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
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
    connect(m_dictation, &DictationPage::navigateRequested, this, &AppWindow::navigateToSettings);
    connect(m_pages->general(), &GeneralSettingsPage::setupRequested,
            m_controller, &ApplicationController::showSetupAssistant);

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

void AppWindow::applyChromeColors()
{
    if (!m_headerStrip) {
        return;
    }
    m_headerStrip->setPalette(settings::kdeHeaderPalette(palette()));

    QPalette dividerPalette(m_headerDividerLine->palette());
    dividerPalette.setColor(QPalette::Window,
                            settings::separatorColor(m_headerStrip->palette()));
    m_headerDividerLine->setPalette(dividerPalette);

    const QColor separator = settings::separatorColor(palette());
    QPalette underlinePalette(m_headerUnderline->palette());
    underlinePalette.setColor(QPalette::Window, separator);
    m_headerUnderline->setPalette(underlinePalette);

    if (QWidget *handle = m_sidebarSplitter ? m_sidebarSplitter->handle(1) : nullptr) {
        QPalette handlePalette(handle->palette());
        handlePalette.setColor(QPalette::Window, separator);
        handle->setPalette(handlePalette);
    }
}

void AppWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    // Track live color-scheme switches: the strip's custom palette roles
    // don't follow the application palette on their own.
    if (event->type() == QEvent::PaletteChange
        || event->type() == QEvent::ApplicationPaletteChange
        || event->type() == QEvent::ThemeChange) {
        applyChromeColors();
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
    }
    return QMainWindow::eventFilter(watched, event);
}

void AppWindow::buildSharedPages()
{
    auto *refinementContent = new QWidget(this);
    refinementContent->setObjectName(QStringLiteral("settingsRiver"));
    refinementContent->setMaximumWidth(560);
    auto *refinementLayout = new QVBoxLayout(refinementContent);
    settings::applyPageMargins(refinementLayout);
    refinementLayout->setSpacing(0);
    refinementLayout->addWidget(detachedContent(m_pages->refinement(), true));
    refinementLayout->addSpacing(settings::groupGap());
    refinementLayout->addWidget(detachedContent(m_pages->providers(), true));
    refinementLayout->addStretch();
    QWidget *refinement = scrollingPage(refinementContent, this);

    auto *tabs = new QTabWidget(this);
    tabs->setObjectName(QStringLiteral("settingsCard"));
    auto addTab = [tabs](QWidget *page, const QString &title) {
        settings::applyPageMargins(page->layout());
        tabs->addTab(scrollingPage(page, tabs), title);
    };
    addTab(m_pages->vocabulary(), QStringLiteral("Vocabulary"));
    addTab(m_pages->corrections(), QStringLiteral("Learned corrections"));
    settings::applyPageMargins(m_pages->bindings()->layout());
    auto *bindingsScroll = scrollingPage(m_pages->bindings(), tabs);
    tabs->addTab(bindingsScroll, QStringLiteral("Replacements && snippets"));
    m_pages->preserveBindingScroll(bindingsScroll);

    auto *vocabularyContent = new QWidget(this);
    vocabularyContent->setObjectName(QStringLiteral("settingsRiver"));
    vocabularyContent->setMaximumWidth(560);
    auto *vocabularyLayout = new QVBoxLayout(vocabularyContent);
    settings::applyPageMargins(vocabularyLayout);
    vocabularyLayout->setSpacing(0);
    vocabularyLayout->addWidget(settings::makePageTitle(QStringLiteral("Vocabulary"), vocabularyContent));
    vocabularyLayout->addSpacing(settings::sectionGap());
    vocabularyLayout->addWidget(settings::makeSectionLabel(QStringLiteral("Words & rules"), vocabularyContent));
    vocabularyLayout->addSpacing(settings::tightSpacing());
    vocabularyLayout->addWidget(tabs, 1);

    m_pageWidgets = {
        m_dictation,
        m_pages->general(),
        m_pages->audio(),
        m_pages->applications(),
        m_pages->output(),
        refinement,
        vocabularyContent,
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
    header->setBackgroundRole(QPalette::Window);
    header->setAutoFillBackground(true);
    m_headerStrip = header;
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
    dividerLine->setAutoFillBackground(true);
    m_headerDividerLine = dividerLine;
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
    root->addWidget(header);

    // Same fill mechanism and color as the splitter handle, so the strip's
    // bottom edge and the sidebar/content hairline match exactly.
    auto *headerUnderline = new QWidget(central);
    headerUnderline->setFixedHeight(1);
    headerUnderline->setAutoFillBackground(true);
    m_headerUnderline = headerUnderline;
    root->addWidget(headerUnderline);

    m_sidebarSplitter = new QSplitter(Qt::Horizontal, central);
    m_sidebarSplitter->setObjectName(QStringLiteral("sidebarSplitter"));
    m_sidebarSplitter->setHandleWidth(1);
    m_sidebarSplitter->setChildrenCollapsible(false);
    auto *sidebar = new QWidget(m_sidebarSplitter);
    sidebar->setBackgroundRole(QPalette::Window);
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
    m_navigation->setBackgroundRole(QPalette::Window);
    m_navigation->setAutoFillBackground(true);
    m_navigation->viewport()->setBackgroundRole(QPalette::Window);
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
        handle->setAutoFillBackground(true);
    }
    applyChromeColors();
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
                QTimer::singleShot(0, this, &AppWindow::applyChromeColors);
            });
    m_sidebarPane = sidebar;
    m_searchSection = searchContainer;
    sidebar->installEventFilter(this);
    connect(m_sidebarSplitter, &QSplitter::splitterMoved, searchContainer,
            [searchContainer, sidebar] { searchContainer->setFixedWidth(sidebar->width()); });
    root->addWidget(m_sidebarSplitter, 1);
    setCentralWidget(central);
    m_sidebarSplitter->setSizes({220, 680});
    const QByteArray splitterState = m_controller->settings()->raw()
                                         .value(QStringLiteral("ui/appWindow/a/splitter"))
                                         .toByteArray();
    if (!splitterState.isEmpty()) {
        m_sidebarSplitter->restoreState(splitterState);
    }
    searchContainer->setFixedWidth(sidebar->width());
    m_navigation->setCurrentRow(0);
    connect(m_navigation, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem *item) {
                if (item && item->data(Qt::UserRole).toInt() >= 0) {
                    m_stack->setCurrentIndex(item->data(Qt::UserRole).toInt());
                }
            });
    connect(m_stack, &QStackedWidget::currentChanged, this, [this](int index) {
        m_pageTitle->setText(kPages.at(index).title);
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
        for (int pageIndex = 0; pageIndex < m_pageWidgets.size(); ++pageIndex) {
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
    SettingsPageSet::SaveFailure failure;
    const bool saved = m_pages->save(false, false, &failure);
    if (!saved) {
        if (failure == SettingsPageSet::SaveFailure::DuplicatePasteRuleIds) {
            m_autoSaveWarningText->setText(
                QStringLiteral("Remove duplicate application paste-rule IDs to save"));
        } else if (failure == SettingsPageSet::SaveFailure::ProviderSecret) {
            m_autoSaveWarningText->setText(QStringLiteral("Could not save provider credentials"));
        } else {
            m_autoSaveWarningText->setText(QStringLiteral("Fix invalid replacement rules to save"));
        }
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
