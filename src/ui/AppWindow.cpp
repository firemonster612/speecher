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
#include <QCheckBox>
#include <QComboBox>
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
#include <QSplitter>
#include <QStackedWidget>
#include <QStyle>
#include <QTabWidget>
#include <QTextDocument>
#include <QTimer>
#include <QToolBar>
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

QIcon themedIcon(const QString &name, QStyle::StandardPixmap fallback, QWidget *widget)
{
    const QIcon icon = QIcon::fromTheme(name);
    return icon.isNull() ? widget->style()->standardIcon(fallback) : icon;
}

QIcon pageIcon(const PageDefinition &page, QWidget *widget)
{
    const QIcon icon = QIcon::fromTheme(
        page.iconName, QIcon::fromTheme(page.fallbackIconName));
    return icon.isNull() ? widget->style()->standardIcon(QStyle::SP_FileIcon) : icon;
}

class DprIconLabel final : public QLabel {
public:
    DprIconLabel(const QIcon &icon, const QSize &size, QWidget *parent)
        : QLabel(parent)
        , m_icon(icon)
        , m_size(size)
    {
        setFixedSize(size);
    }

protected:
    void showEvent(QShowEvent *event) override
    {
        QLabel::showEvent(event);
        setPixmap(m_icon.pixmap(m_size, devicePixelRatio()));
    }

private:
    QIcon m_icon;
    QSize m_size;
};

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
    settings::applyLabelHierarchy(this);

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
        titles << page.title;
    }
    return titles;
}

int AppWindow::pageCount() const { return kPages.size(); }

void AppWindow::navigateToSettings(AppPageId page)
{
    const int settingsIndex = static_cast<int>(page);
    if (m_prototype == QStringLiteral("c")) {
        showCompactPage(settingsIndex);
    } else if (m_navigation) {
        for (int row = 0; row < m_navigation->count(); ++row) {
            QListWidgetItem *item = m_navigation->item(row);
            if (item->data(Qt::UserRole).toInt() == settingsIndex + 1) {
                m_navigation->setCurrentItem(item);
                break;
            }
        }
    } else if (m_navigationActions) {
        for (QAction *action : m_navigationActions->actions()) {
            if (action->data().toInt() == settingsIndex + 1) {
                action->trigger();
                break;
            }
        }
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

void AppWindow::buildSharedPages()
{
    auto *refinementContent = new QWidget(this);
    auto *refinementLayout = new QVBoxLayout(refinementContent);
    settings::applyPageMargins(refinementLayout);
    refinementLayout->setSpacing(0);
    refinementLayout->addWidget(settings::makePageTitle(QStringLiteral("Refinement"), refinementContent));
    refinementLayout->addSpacing(settings::sectionGap());
    refinementLayout->addWidget(settings::makeSectionLabel(QStringLiteral("Refinement"), refinementContent));
    refinementLayout->addSpacing(settings::tightSpacing());
    refinementLayout->addWidget(detachedContent(m_pages->refinement(), true));
    refinementLayout->addSpacing(settings::groupGap());
    refinementLayout->addWidget(settings::makeCenteredSeparator(refinementContent));
    refinementLayout->addSpacing(settings::groupGap());
    refinementLayout->addWidget(settings::makeSectionLabel(QStringLiteral("Provider accounts"), refinementContent));
    refinementLayout->addSpacing(settings::tightSpacing());
    refinementLayout->addWidget(detachedContent(m_pages->providers(), true));
    refinementLayout->addStretch();
    QWidget *refinement = scrollingPage(refinementContent, this);

    auto *tabs = new QTabWidget(this);
    auto addTab = [tabs](QWidget *page, const QString &title) {
        settings::applyPageMargins(page->layout());
        tabs->addTab(scrollingPage(page, tabs), title);
    };
    addTab(m_pages->vocabulary(), QStringLiteral("Vocabulary"));
    addTab(m_pages->corrections(), QStringLiteral("Learned corrections"));
    settings::applyPageMargins(m_pages->bindings()->layout());
    auto *bindingsScroll = scrollingPage(m_pages->bindings(), tabs);
    tabs->addTab(bindingsScroll, QStringLiteral("Replacements & snippets"));
    m_pages->preserveBindingScroll(bindingsScroll);

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
    header->setPalette(settings::kdeHeaderPalette(palette()));
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

    auto *headerRight = new QWidget(header);
    auto *headerRightLayout = new QHBoxLayout(headerRight);
    headerRightLayout->setContentsMargins(settings::relatedSpacing(),
                                          settings::relatedSpacing(),
                                          settings::relatedSpacing(),
                                          settings::relatedSpacing());
    m_pageTitle = settings::makePageTitle(kPages.first().title, headerRight);
    headerRightLayout->addWidget(m_pageTitle);
    headerRightLayout->addStretch();
    headerRightLayout->addWidget(m_dictation->toggleButton());
    headerLayout->addWidget(headerRight, 1);
    root->addWidget(header);

    auto *headerLine = new QFrame(central);
    headerLine->setFrameShape(QFrame::HLine);
    headerLine->setFrameShadow(QFrame::Plain);
    headerLine->setFixedHeight(1);
    root->addWidget(headerLine);

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
    sidebarLayout->addWidget(createPrototypeSwitcher(sidebar), 0, Qt::AlignLeft);
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
        m_dictation->toggleButton()->setVisible(index == 0);
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

void AppWindow::buildToolbarShell()
{
    resize(900, 640);
    setMinimumSize(760, 520);
    auto *toolbar = addToolBar(QStringLiteral("Pages"));
    toolbar->setMovable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    const int toolbarIconSize = style()->pixelMetric(QStyle::PM_ToolBarIconSize, nullptr, toolbar);
    toolbar->setIconSize(QSize(toolbarIconSize, toolbarIconSize));
    m_navigationActions = new QActionGroup(this);
    m_navigationActions->setExclusive(true);
    for (int index = 0; index < kPages.size(); ++index) {
        const auto &page = kPages.at(index);
        QAction *action = toolbar->addAction(
            pageIcon(page, toolbar), page.title);
        action->setCheckable(true);
        action->setData(index);
        m_navigationActions->addAction(action);
        connect(action, &QAction::triggered, this, [this, index] { m_stack->setCurrentIndex(index); });
        if (index == 0) action->setChecked(true);
    }

    auto *central = new QWidget(this);
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    m_stack = new QStackedWidget(central);
    m_stack->setObjectName(QStringLiteral("appPageStack"));
    for (QWidget *page : std::as_const(m_pageWidgets)) m_stack->addWidget(page);
    root->addWidget(createPageHeader(central));
    root->addWidget(m_stack, 1);
    root->addWidget(createPendingBanner(central));
    toolbar->addSeparator();
    toolbar->addWidget(createPrototypeSwitcher(toolbar));
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
    m_compactList->setBackgroundRole(QPalette::Base);
    m_compactList->setAutoFillBackground(true);
    m_compactList->setFrameShape(QFrame::NoFrame);
    m_compactList->setIconSize(QSize(22, 22));
    for (int index = 1; index < kPages.size(); ++index) {
        const auto &page = kPages.at(index);
        auto *item = new QListWidgetItem(m_compactList);
        item->setIcon(pageIcon(page, m_compactList));
        item->setData(Qt::UserRole, index - 1);
        item->setSizeHint(QSize(0, 36));
        auto *row = new QWidget(m_compactList);
        auto *rowLayout = new QHBoxLayout(row);
        const int rowSpacing = qMax(
            0, row->style()->pixelMetric(QStyle::PM_LayoutHorizontalSpacing, nullptr, row));
        rowLayout->setContentsMargins(20 + rowSpacing, 0, 0, 0);
        auto *title = new QLabel(page.title, row);
        auto *arrow = new DprIconLabel(
            themedIcon(QStringLiteral("go-next"), QStyle::SP_ArrowRight, row), QSize(16, 16), row);
        rowLayout->addWidget(title);
        rowLayout->addStretch();
        rowLayout->addWidget(arrow);
        m_compactList->setItemWidget(item, row);
    }
    auto *homeSeparator = new QFrame(home);
    homeSeparator->setFrameShape(QFrame::HLine);
    homeSeparator->setFrameShadow(QFrame::Plain);
    homeLayout->addWidget(homeSeparator);
    homeLayout->addWidget(m_compactList);

    auto *drill = new QWidget(m_compactStack);
    auto *drillLayout = new QVBoxLayout(drill);
    drillLayout->setContentsMargins(0, 0, 0, 0);
    drillLayout->setSpacing(0);
    auto *header = new QWidget(drill);
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(settings::relatedSpacing(),
                                     settings::relatedSpacing(),
                                     settings::relatedSpacing(),
                                     settings::relatedSpacing());
    auto *back = new QPushButton(QStringLiteral("Back"), header);
    back->setIcon(themedIcon(QStringLiteral("go-previous"), QStyle::SP_ArrowLeft, back));
    m_drillTitle = settings::makePageTitle(QString(), header);
    headerLayout->addWidget(back);
    headerLayout->addWidget(m_drillTitle);
    headerLayout->addStretch();
    drillLayout->addWidget(header);
    auto *headerLine = new QFrame(drill);
    headerLine->setFrameShape(QFrame::HLine);
    headerLine->setFrameShadow(QFrame::Plain);
    drillLayout->addWidget(headerLine);
    m_drillPages = new QStackedWidget(drill);
    for (int index = 1; index < m_pageWidgets.size(); ++index) {
        QWidget *page = m_pageWidgets.at(index);
        removeEmbeddedPageTitle(page);
        m_drillPages->addWidget(page);
    }
    drillLayout->addWidget(m_drillPages, 1);
    drillLayout->addWidget(createPendingBanner(drill, true));

    m_compactStack->addWidget(home);
    m_compactStack->addWidget(drill);
    root->addWidget(m_compactStack, 1);
    root->addWidget(createPrototypeSwitcher(central));
    setCentralWidget(central);
    connect(m_compactList, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
        showCompactPage(item->data(Qt::UserRole).toInt());
    });
    connect(m_compactList, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        showCompactPage(item->data(Qt::UserRole).toInt());
    });
    connect(back, &QPushButton::clicked, this, [this] {
        if (m_pages->hasChanges()) {
            m_pendingCompactBack = true;
            updatePendingBanner();
            m_pendingApplyButton->setFocus(Qt::OtherFocusReason);
        } else {
            finishCompactBack();
        }
    });
}

QWidget *AppWindow::createPrototypeSwitcher(QWidget *parent)
{
    auto *bar = new QWidget(parent);
    auto *layout = new QHBoxLayout(bar);
    layout->setContentsMargins(settings::relatedSpacing(),
                               settings::tightSpacing(),
                               settings::relatedSpacing(),
                               settings::tightSpacing());
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

QWidget *AppWindow::createPageHeader(QWidget *parent)
{
    auto *band = new QWidget(parent);
    band->setObjectName(QStringLiteral("pageHeaderBand"));
    auto *layout = new QVBoxLayout(band);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *content = new QWidget(band);
    auto *contentLayout = new QHBoxLayout(content);
    contentLayout->setContentsMargins(settings::relatedSpacing(),
                                      settings::relatedSpacing(),
                                      settings::relatedSpacing(),
                                      settings::relatedSpacing());
    m_pageTitle = settings::makePageTitle(kPages.first().title, content);
    contentLayout->addWidget(m_pageTitle);
    contentLayout->addStretch();
    contentLayout->addWidget(m_dictation->toggleButton());
    layout->addWidget(content);

    auto *line = new QFrame(band);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Plain);
    layout->addWidget(line);

    connect(m_stack, &QStackedWidget::currentChanged, this, [this](int index) {
        m_pageTitle->setText(kPages.at(index).title);
        m_dictation->toggleButton()->setVisible(index == 0);
    });
    return band;
}

QWidget *AppWindow::createPendingBanner(QWidget *parent, bool compact)
{
    m_pendingBanner = new QFrame(parent);
    m_pendingBanner->setObjectName(QStringLiteral("pendingChangesBanner"));
    m_pendingBanner->setFrameShape(QFrame::StyledPanel);
    m_pendingBanner->setFrameShadow(QFrame::Plain);
    auto *layout = new QHBoxLayout(m_pendingBanner);
    layout->setContentsMargins(compact ? 6 : 12, 6, compact ? 6 : 12, 6);
    if (compact) {
        auto *icon = new DprIconLabel(
            style()->standardIcon(QStyle::SP_MessageBoxWarning), QSize(16, 16), m_pendingBanner);
        layout->addWidget(icon);
    }
    auto *message = new QLabel(QStringLiteral("You have unsaved changes"), m_pendingBanner);
    if (compact) {
        QFont font = message->font();
        font.setBold(true);
        message->setFont(font);
    }
    layout->addWidget(message);
    layout->addStretch();
    auto *apply = new QPushButton(QStringLiteral("Apply"), m_pendingBanner);
    if (compact) m_pendingApplyButton = apply;
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

void AppWindow::updatePendingBanner()
{
    if (m_pendingBanner) m_pendingBanner->setVisible(m_pages->hasChanges());
}

void AppWindow::showCompactPage(int page)
{
    const int boundedPage = qBound(0, page, kPages.size() - 2);
    m_drillPages->setCurrentIndex(boundedPage);
    m_drillTitle->setText(kPages.at(boundedPage + 1).title);
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
    if (m_sidebarSplitter) {
        m_controller->settings()->raw().setValue(
            QStringLiteral("ui/appWindow/a/splitter"), m_sidebarSplitter->saveState());
    }
}

} // namespace speecher
