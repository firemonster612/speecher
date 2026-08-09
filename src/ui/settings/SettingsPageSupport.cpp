#include "ui/settings/SettingsPageSupport.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFontMetrics>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QPushButton>
#include <QScrollArea>
#include <QSizePolicy>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QStyle>
#include <QVBoxLayout>

#include <functional>
#include <utility>

#ifdef SPEECHER_WITH_KPAGEWIDGET
#include <KPageWidget>
#endif
#ifdef SPEECHER_WITH_KCOLORSCHEME
#include <KColorScheme>
#endif

namespace speecher::settings {

static bool useKdeSettingsPages()
{
#ifdef SPEECHER_WITH_KPAGEWIDGET
    return qEnvironmentVariable("XDG_CURRENT_DESKTOP").contains(
        QStringLiteral("KDE"),
        Qt::CaseInsensitive);
#else
    return false;
#endif
}

#ifdef SPEECHER_WITH_KPAGEWIDGET
static QColor kdeHeaderSeparatorColor(const QPalette &palette)
{
#ifdef SPEECHER_WITH_KCOLORSCHEME
    const KColorScheme headerColors(palette.currentColorGroup(), KColorScheme::Header);
    const QColor background = headerColors.background().color();
    const QColor text = headerColors.foreground().color();
#else
    const QColor background = palette.color(QPalette::Window);
    const QColor text = palette.color(QPalette::WindowText);
#endif
    constexpr qreal frameContrast = 0.2;
    return QColor::fromRgbF(
        background.redF() + (text.redF() - background.redF()) * frameContrast,
        background.greenF() + (text.greenF() - background.greenF()) * frameContrast,
        background.blueF() + (text.blueF() - background.blueF()) * frameContrast);
}

class HeaderSeparator final : public QWidget {
public:
    explicit HeaderSeparator(QWidget *parent)
        : QWidget(parent)
    {
        setObjectName(QStringLiteral("settingsHeaderSeparator"));
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter(this).fillRect(rect(), kdeHeaderSeparatorColor(palette()));
    }
};

class SidebarResizeHandle final : public QWidget {
public:
    SidebarResizeHandle(std::function<int()> currentWidth,
                        std::function<int()> headerHeight,
                        std::function<void(int)> resizeSidebar,
                        QWidget *parent)
        : QWidget(parent)
        , m_currentWidth(std::move(currentWidth))
        , m_headerHeight(std::move(headerHeight))
        , m_resizeSidebar(std::move(resizeSidebar))
    {
        setObjectName(QStringLiteral("settingsSidebarResizeHandle"));
        setCursor(Qt::SplitHCursor);
        setFixedWidth(7);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setPen(kdeHeaderSeparatorColor(palette()));

        const int center = width() / 2;
        const int headerHeight = qBound(0, m_headerHeight(), height());
        const int inset = qMax(1, style()->pixelMetric(QStyle::PM_LayoutTopMargin, nullptr, this));
        const int headerLineEnd = headerHeight - inset - 1;
        if (headerLineEnd >= inset) {
            painter.drawLine(center, inset, center, headerLineEnd);
        }
        if (headerHeight > 0) {
            painter.drawLine(center, headerHeight - 1, center, height() - 1);
        }
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton) {
            QWidget::mousePressEvent(event);
            return;
        }
        m_dragStart = event->globalPosition().x();
        m_startWidth = m_currentWidth();
        event->accept();
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (!(event->buttons() & Qt::LeftButton)) {
            QWidget::mouseMoveEvent(event);
            return;
        }
        m_resizeSidebar(m_startWidth + qRound(event->globalPosition().x() - m_dragStart));
        event->accept();
    }

private:
    std::function<int()> m_currentWidth;
    std::function<int()> m_headerHeight;
    std::function<void(int)> m_resizeSidebar;
    qreal m_dragStart = 0;
    int m_startWidth = 0;
};

class ResizableKPageWidget final : public KPageWidget {
public:
    explicit ResizableKPageWidget(QWidget *parent)
        : KPageWidget(parent)
        , m_headerSeparator(new HeaderSeparator(this))
        , m_resizeHandle(new SidebarResizeHandle(
              [this] {
                  QWidget *sidebar = searchContainer();
                  return sidebar ? sidebar->width() : 0;
              },
              [this] {
                  return m_pageSeparator ? m_pageSeparator->geometry().bottom() + 1 : 0;
              },
              [this](int width) { setSidebarWidth(width); },
              this))
    {
        const auto frames = findChildren<QFrame *>(QString(), Qt::FindDirectChildrenOnly);
        for (QFrame *frame : frames) {
            if (frame->frameShape() == QFrame::HLine) {
                m_pageSeparator = frame;
                m_pageSeparator->setFrameShape(QFrame::NoFrame);
                break;
            }
        }
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        KPageWidget::resizeEvent(event);
        if (m_sidebarWidth >= 0) {
            setSidebarWidth(m_sidebarWidth);
        }
        positionSeparators();
    }

    void showEvent(QShowEvent *event) override
    {
        KPageWidget::showEvent(event);
        positionSeparators();
    }

private:
    QWidget *searchContainer() const
    {
        return findChild<QWidget *>(QStringLiteral("KPageView::Search"));
    }

    QAbstractItemView *navigationView() const
    {
        return findChild<QAbstractItemView *>(QString(), Qt::FindDirectChildrenOnly);
    }

    void setSidebarWidth(int requestedWidth)
    {
        QWidget *sidebar = searchContainer();
        QAbstractItemView *navigation = navigationView();
        if (!sidebar || !navigation) {
            return;
        }
        constexpr int minimumSidebarWidth = 180;
        constexpr int minimumPageWidth = 480;
        m_sidebarWidth = qBound(minimumSidebarWidth,
                                requestedWidth,
                                qMax(minimumSidebarWidth, width() - minimumPageWidth));
        sidebar->setFixedWidth(m_sidebarWidth);
        navigation->setFixedWidth(m_sidebarWidth);
        if (layout()) {
            layout()->activate();
        }
        positionSeparators();
    }

    void positionSeparators()
    {
        QWidget *sidebar = searchContainer();
        if (!sidebar || !sidebar->isVisible() || !m_pageSeparator) {
            m_headerSeparator->hide();
            m_resizeHandle->hide();
            return;
        }
        m_headerSeparator->setGeometry(0, m_pageSeparator->geometry().y(), width(), 1);
        m_headerSeparator->show();
        m_headerSeparator->raise();

        const int x = sidebar->geometry().right() + 1 - m_resizeHandle->width() / 2;
        m_resizeHandle->setGeometry(x, 0, m_resizeHandle->width(), height());
        m_resizeHandle->show();
        m_resizeHandle->raise();
    }

    HeaderSeparator *m_headerSeparator;
    SidebarResizeHandle *m_resizeHandle;
    QFrame *m_pageSeparator = nullptr;
    int m_sidebarWidth = -1;
};
#endif

QFrame *makeSeparator(QWidget *parent)
{
    auto *line = new QFrame(parent);
    line->setObjectName(QStringLiteral("settingsSeparator"));
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    line->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    return line;
}

QWidget *makeCenteredSeparator(QWidget *parent)
{
    auto *container = new QWidget(parent);
    auto *layout = new QHBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addStretch(1);
    layout->addWidget(makeSeparator(container), 3);
    layout->addStretch(1);
    return container;
}

void configureFormLayout(QFormLayout *form)
{
    form->setRowWrapPolicy(QFormLayout::DontWrapRows);
    form->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
    form->setFormAlignment(Qt::AlignHCenter | Qt::AlignTop);
    form->setLabelAlignment(Qt::AlignRight);
}

QFrame *makeRow(const QString &label,
                const QString &description,
                QWidget *control,
                QWidget *parent,
                QWidget *titleAccessory)
{
    auto *row = new QFrame(parent);
    row->setObjectName(QStringLiteral("settingsRow"));
    row->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);

    auto *title = new QLabel(label.endsWith(QLatin1Char(':'))
                                 ? label
                                 : label + QLatin1Char(':'),
                             row);
    title->setObjectName(QStringLiteral("rowTitle"));
    title->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    QWidget *labelField = title;
    if (titleAccessory) {
        auto *titleRow = new QWidget(row);
        titleRow->setObjectName(QStringLiteral("rowText"));
        auto *titleLayout = new QHBoxLayout(titleRow);
        titleLayout->setContentsMargins(0, 0, 0, 0);
        titleLayout->addWidget(title, 0, Qt::AlignVCenter);
        titleLayout->addWidget(titleAccessory, 0, Qt::AlignVCenter);
        labelField = titleRow;
    }
    labelField->setObjectName(QStringLiteral("rowLabelCell"));

    auto *fieldLayout = new QVBoxLayout(row);
    fieldLayout->setContentsMargins(0, 0, 0, 0);
    fieldLayout->setSpacing(tightSpacing());
    if (control->sizePolicy().horizontalPolicy() == QSizePolicy::Expanding) {
        fieldLayout->addWidget(control);
    } else {
        fieldLayout->addWidget(control, 0, Qt::AlignLeft);
    }

    if (auto *checkBox = qobject_cast<QCheckBox *>(control); checkBox && !description.isEmpty()) {
        checkBox->setText(description);
    } else if (!description.isEmpty()) {
        auto *subtitle = new QLabel(description, row);
        subtitle->setObjectName(QStringLiteral("rowDescription"));
        subtitle->setWordWrap(true);
        const int naturalTextWidth = subtitle->fontMetrics().horizontalAdvance(description);
        subtitle->setFixedWidth(qMin(
            naturalTextWidth, subtitle->fontMetrics().averageCharWidth() * 45));
        subtitle->setForegroundRole(QPalette::PlaceholderText);
        fieldLayout->addWidget(subtitle);
    }
    return row;
}

void addRow(QFormLayout *layout, QFrame *row, QWidget *parent, bool addSeparator)
{
    QWidget *label = row->findChild<QWidget *>(QStringLiteral("rowLabelCell"),
                                               Qt::FindDirectChildrenOnly);
    layout->addRow(label, row);
    if (addSeparator) {
        layout->addRow(makeCenteredSeparator(parent));
    }
}

void selectData(QComboBox *combo, const QString &data)
{
    const int index = combo->findData(data);
    combo->setCurrentIndex(index >= 0 ? index : 0);
}

void selectEditableText(QComboBox *combo, const QString &text)
{
    const QString trimmed = text.trimmed();
    const int dataIndex = combo->findData(trimmed);
    if (dataIndex >= 0) {
        combo->setCurrentIndex(dataIndex);
        return;
    }
    const int index = combo->findText(trimmed);
    if (index >= 0) {
        combo->setCurrentIndex(index);
        return;
    }
    combo->addItem(trimmed, trimmed);
    combo->setCurrentIndex(combo->count() - 1);
}

QString editableComboValue(const QComboBox *combo)
{
    const int index = combo->currentIndex();
    const QString text = combo->currentText().trimmed();
    if (index >= 0 && text == combo->itemText(index)) {
        const QString data = combo->itemData(index).toString().trimmed();
        if (!data.isEmpty()) {
            return data;
        }
    }
    return text;
}

void setComboItemEnabled(QComboBox *combo, int index, bool enabled, const QString &toolTip)
{
    auto *model = qobject_cast<QStandardItemModel *>(combo->model());
    if (!model || index < 0) {
        return;
    }
    QStandardItem *item = model->item(index);
    if (!item) {
        return;
    }
    item->setEnabled(enabled);
    item->setToolTip(toolTip);
}

int tightSpacing() { return 4; }
int relatedSpacing() { return 8; }
int groupGap() { return 18; }
int sectionGap() { return 24; }

void applyPageMargins(QLayout *layout)
{
    QWidget *widget = layout->parentWidget();
    QStyle *style = widget ? widget->style() : QApplication::style();
    layout->setContentsMargins(
        style->pixelMetric(QStyle::PM_LayoutLeftMargin, nullptr, widget),
        style->pixelMetric(QStyle::PM_LayoutTopMargin, nullptr, widget),
        style->pixelMetric(QStyle::PM_LayoutRightMargin, nullptr, widget),
        style->pixelMetric(QStyle::PM_LayoutBottomMargin, nullptr, widget));
}

void applyLabelHierarchy(QWidget *root)
{
    for (QLabel *label : root->findChildren<QLabel *>()) {
        if (label->objectName() == QStringLiteral("subsectionLabel")) {
            QFont font = label->font();
            font.setBold(true);
            label->setFont(font);
        } else if (label->objectName() == QStringLiteral("rowDescription")
                   || label->objectName() == QStringLiteral("noteText")) {
            label->setForegroundRole(QPalette::PlaceholderText);
        }
    }
}

QLabel *makeSectionLabel(const QString &text, QWidget *parent)
{
    auto *section = new QLabel(text, parent);
    section->setObjectName(QStringLiteral("sectionLabel"));
    section->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    QFont font = section->font();
    font.setBold(true);
    section->setFont(font);
    return section;
}

QLabel *makePageTitle(const QString &text, QWidget *parent)
{
    auto *title = new QLabel(text, parent);
    title->setObjectName(QStringLiteral("pageTitle"));
    title->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    QFont font = QApplication::font();
    if (font.pointSizeF() > 0) {
        font.setPointSizeF(font.pointSizeF() * 1.4);
    } else if (font.pixelSize() > 0) {
        font.setPixelSize(qRound(font.pixelSize() * 1.4));
    }
    font.setBold(false);
    title->setFont(font);
    return title;
}

QFrame *makeSettingsCard(QWidget *parent)
{
    auto *card = new QFrame(parent);
    card->setObjectName(QStringLiteral("settingsCard"));
    auto *layout = new QFormLayout(card);
    layout->setContentsMargins(0, 0, 0, 0);
    configureFormLayout(layout);
    return card;
}

QVBoxLayout *makeSettingsPage(QScrollArea *scroll)
{
    scroll->setObjectName(QStringLiteral("settingsScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setBackgroundRole(QPalette::Window);
    scroll->viewport()->setBackgroundRole(QPalette::Window);

    auto *page = new QWidget(scroll);
    page->setAutoFillBackground(false);
    auto *layout = new QVBoxLayout(page);
    applyPageMargins(layout);
    scroll->setWidget(page);
    return layout;
}

void addPageContainer(QHBoxLayout *layout,
                      const QList<QPair<QString, QString>> &categories,
                      const QList<QWidget *> &pages,
                      QListWidget **categoriesWidget,
                      QStackedWidget **pagesWidget,
                      QWidget *parent)
{
#ifdef SPEECHER_WITH_KPAGEWIDGET
    if (useKdeSettingsPages()) {
        auto *settingsPages = new ResizableKPageWidget(parent);
        settingsPages->setObjectName(QStringLiteral("settingsPages"));
        settingsPages->setFaceType(KPageView::FlatList);
        for (qsizetype index = 0; index < categories.size(); ++index) {
            const auto &[label, iconName] = categories.at(index);
            KPageWidgetItem *item = settingsPages->addPage(pages.at(index), label);
            item->setIcon(QIcon::fromTheme(iconName));
            item->setHeaderVisible(false);
        }
        layout->addWidget(settingsPages, 1);
        return;
    }
#endif
    *categoriesWidget = new QListWidget(parent);
    *pagesWidget = new QStackedWidget(parent);
    for (qsizetype index = 0; index < categories.size(); ++index) {
        const auto &[label, iconName] = categories.at(index);
        new QListWidgetItem(QIcon::fromTheme(iconName), label, *categoriesWidget);
        (*pagesWidget)->addWidget(pages.at(index));
    }
    (*categoriesWidget)->setObjectName(QStringLiteral("settingsCategories"));
    (*categoriesWidget)->setBackgroundRole(QPalette::Base);
    (*categoriesWidget)->setAutoFillBackground(true);
    (*categoriesWidget)->setFrameShape(QFrame::NoFrame);
    (*categoriesWidget)->setSelectionMode(QAbstractItemView::SingleSelection);
    (*categoriesWidget)->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    (*categoriesWidget)->setMinimumWidth(180);
    (*categoriesWidget)->setMaximumWidth(240);
    (*categoriesWidget)->setIconSize(QSize(18, 18));
    QObject::connect(*categoriesWidget,
                     &QListWidget::currentRowChanged,
                     *pagesWidget,
                     &QStackedWidget::setCurrentIndex);
    (*categoriesWidget)->setCurrentRow(0);
    layout->addWidget(*categoriesWidget);
    auto *separator = new QFrame(parent);
    separator->setFrameShape(QFrame::VLine);
    separator->setFrameShadow(QFrame::Plain);
    layout->addWidget(separator);
    layout->addWidget(*pagesWidget, 1);
}

} // namespace speecher::settings
