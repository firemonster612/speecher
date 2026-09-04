#include "ui/settings/FormCard.h"

#include "ui/settings/SettingsPageSupport.h"

#include <QAbstractButton>
#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QSizePolicy>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QTimer>
#include <QVBoxLayout>

namespace speecher::settings {

namespace {

constexpr int kFlashMilliseconds = 1100;
constexpr int kCardColumnStretch = 1000;

QLabel *makeSubtitleLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("rowSubtitle"));
    label->setWordWrap(true);
    label->setForegroundRole(QPalette::PlaceholderText);
    label->setVisible(!text.isEmpty());
    return label;
}

QString controlText(const QWidget *control)
{
    if (!control) {
        return {};
    }
    QStringList words;
    if (const auto *combo = qobject_cast<const QComboBox *>(control)) {
        for (int index = 0; index < combo->count(); ++index) {
            words.append(combo->itemText(index));
        }
    } else if (const auto *button = qobject_cast<const QAbstractButton *>(control)) {
        words.append(button->text());
    } else if (const auto *label = qobject_cast<const QLabel *>(control)) {
        words.append(label->text());
    }
    for (const QAbstractButton *button : control->findChildren<QAbstractButton *>()) {
        words.append(button->text());
    }
    return words.join(QLatin1Char('\n'));
}

} // namespace

FormRow::FormRow(const QString &title, const QString &subtitle, QWidget *parent)
    : QWidget(parent)
    , m_title(new QLabel(title, this))
    , m_subtitle(makeSubtitleLabel(subtitle, this))
    , m_textColumn(new QWidget(this))
    , m_textLayout(new QVBoxLayout(m_textColumn))
    , m_headLayout(new QHBoxLayout)
    , m_layout(new QVBoxLayout(this))
    , m_flashTimer(new QTimer(this))
{
    setObjectName(QStringLiteral("formRow"));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_layout->setContentsMargins(rowHorizontalPadding(),
                                 rowVerticalPadding(),
                                 rowHorizontalPadding(),
                                 rowVerticalPadding());
    m_layout->setSpacing(relatedSpacing());

    m_title->setObjectName(QStringLiteral("rowTitle"));
    m_title->setWordWrap(true);
    m_title->setVisible(!title.isEmpty());
    m_textLayout->setContentsMargins(0, 0, 0, 0);
    m_textLayout->setSpacing(tightSpacing());
    m_textLayout->addWidget(m_title);
    m_textLayout->addWidget(m_subtitle);
    m_textColumn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    m_headLayout->setContentsMargins(0, 0, 0, 0);
    m_headLayout->setSpacing(gridUnit());
    m_headLayout->addWidget(m_textColumn, 1, Qt::AlignVCenter);
    m_layout->addLayout(m_headLayout);
    // A row that is nothing but its editor has no head to show.
    m_textColumn->setVisible(!title.isEmpty() || !subtitle.isEmpty());
    m_flashTimer->setSingleShot(true);
    m_flashTimer->setInterval(kFlashMilliseconds);
    connect(m_flashTimer, &QTimer::timeout, this, [this] {
        m_flashing = false;
        applyFlashPalette(false);
        update();
    });
}

void FormRow::setControl(QWidget *control)
{
    m_control = control;
    control->setParent(this);
    // Right-aligned, with the shared minimum so the column of controls lines
    // up when their contents are short.
    if (qobject_cast<QComboBox *>(control) || control->inherits("QAbstractSpinBox")) {
        control->setMinimumWidth(qMax(control->minimumWidth(), controlMinimumWidth()));
    }
    if (auto *label = qobject_cast<QLabel *>(control)) {
        label->setWordWrap(true);
        label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        label->setMaximumWidth(valueMaximumWidth());
    }
    m_headLayout->addWidget(control, 0, Qt::AlignRight | Qt::AlignVCenter);
    m_title->setBuddy(control);
    m_textColumn->show();
}

void FormRow::setEditor(QWidget *editor)
{
    m_editor = editor;
    editor->setParent(this);
    editor->setSizePolicy(QSizePolicy::Expanding, editor->sizePolicy().verticalPolicy());
    m_layout->addWidget(editor);
    m_title->setBuddy(editor);
}

void FormRow::setDetail(QWidget *detail)
{
    m_detail = detail;
    detail->setParent(m_textColumn);
    if (auto *label = qobject_cast<QLabel *>(detail)) {
        label->setForegroundRole(QPalette::PlaceholderText);
    }
    m_textLayout->addWidget(detail);
    m_textColumn->show();
}

void FormRow::setSubtitle(const QString &text)
{
    m_subtitle->setText(text);
    m_subtitle->setVisible(!text.isEmpty());
}

QString FormRow::title() const
{
    return m_title->text();
}

QString FormRow::subtitle() const
{
    return m_subtitle->text();
}

QString FormRow::searchText() const
{
    QStringList words{m_title->text(), m_subtitle->text(), controlText(m_control)};
    if (m_detail) {
        if (const auto *label = qobject_cast<const QLabel *>(m_detail)) {
            words.append(label->text());
        }
    }
    words.removeAll(QString());
    return words.join(QLatin1Char('\n'));
}

void FormRow::flash()
{
    m_flashing = true;
    applyFlashPalette(true);
    update();
    m_flashTimer->start();
}

// The labels read in the highlight's own text colour while the highlight is
// behind them, then go back to what they were.
void FormRow::applyFlashPalette(bool flashing)
{
    m_title->setForegroundRole(flashing ? QPalette::HighlightedText : QPalette::WindowText);
    m_subtitle->setForegroundRole(flashing ? QPalette::HighlightedText : QPalette::PlaceholderText);
    if (auto *label = qobject_cast<QLabel *>(m_detail)) {
        label->setForegroundRole(flashing ? QPalette::HighlightedText : QPalette::PlaceholderText);
    }
}

void FormRow::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && isEnabled()) {
        if (auto *toggle = qobject_cast<QCheckBox *>(m_control); toggle && toggle->isEnabled()) {
            m_togglePressed = true;
            event->accept();
            return;
        }
    }
    m_togglePressed = false;
    QWidget::mousePressEvent(event);
}

void FormRow::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_togglePressed) {
        m_togglePressed = false;
        if (rect().contains(event->position().toPoint()) && isEnabled()) {
            if (auto *toggle = qobject_cast<QCheckBox *>(m_control); toggle && toggle->isEnabled()) {
                toggle->toggle();
                toggle->setFocus(Qt::MouseFocusReason);
            }
        }
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void FormRow::paintEvent(QPaintEvent *event)
{
    QWidget::paintEvent(event);
    if (!m_flashing) {
        return;
    }
    // The style's own selected-item panel, as a list view draws a chosen row.
    QStyleOptionViewItem option;
    option.initFrom(this);
    option.rect = rect();
    option.state |= QStyle::State_Selected | QStyle::State_Active | QStyle::State_Enabled;
    option.viewItemPosition = QStyleOptionViewItem::OnlyOne;
    option.showDecorationSelected = true;
    QPainter painter(this);
    style()->drawPrimitive(QStyle::PE_PanelItemViewItem, &option, &painter, this);
}

SettingsCard::SettingsCard(const QString &title, const QString &description, QWidget *parent)
    : QWidget(parent)
    , m_title(new QLabel(title, this))
    , m_description(makeSubtitleLabel(description, this))
    , m_body(new QGroupBox(this))
    , m_bodyLayout(new QVBoxLayout(m_body))
{
    setObjectName(QStringLiteral("settingsCard"));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(relatedSpacing());

    auto *header = new QWidget(this);
    header->setObjectName(QStringLiteral("cardHeader"));
    auto *headerLayout = new QVBoxLayout(header);
    // The header text lines up with the row text inside the card.
    headerLayout->setContentsMargins(rowHorizontalPadding(), 0, rowHorizontalPadding(), 0);
    headerLayout->setSpacing(tightSpacing());
    m_title->setObjectName(QStringLiteral("cardTitle"));
    QFont titleFont = m_title->font();
    titleFont.setBold(true);
    m_title->setFont(titleFont);
    m_title->setWordWrap(true);
    m_description->setObjectName(QStringLiteral("cardDescription"));
    headerLayout->addWidget(m_title);
    headerLayout->addWidget(m_description);
    header->setVisible(!title.isEmpty() || !description.isEmpty());
    layout->addWidget(header);

    m_body->setObjectName(QStringLiteral("cardBody"));
    m_body->setFlat(false);
    m_bodyLayout->setContentsMargins(0, 0, 0, 0);
    m_bodyLayout->setSpacing(0);
    layout->addWidget(m_body);
}

QString SettingsCard::title() const
{
    return m_title->text();
}

void SettingsCard::addRow(QWidget *row)
{
    row->setParent(m_body);
    if (!m_rows.isEmpty()) {
        auto *separator = new QFrame(m_body);
        separator->setObjectName(QStringLiteral("rowSeparator"));
        separator->setFrameShape(QFrame::HLine);
        separator->setFrameShadow(QFrame::Sunken);
        m_separators.append(separator);
        m_bodyLayout->addWidget(separator);
    }
    m_rows.append(row);
    m_bodyLayout->addWidget(row);
    updateSeparators();
}

void SettingsCard::updateSeparators()
{
    bool anyShownAbove = false;
    for (int index = 0; index < m_rows.size(); ++index) {
        const bool shown = m_rows.at(index)->isVisibleTo(m_body);
        if (index > 0) {
            m_separators.at(index - 1)->setVisible(shown && anyShownAbove);
        }
        anyShownAbove = anyShownAbove || shown;
    }
}

QVBoxLayout *makeCardColumn(QBoxLayout *pageLayout, QWidget *parent)
{
    auto *row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    auto *column = new QWidget(parent);
    column->setObjectName(QStringLiteral("cardColumn"));
    column->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    column->setMaximumWidth(cardMaximumWidth());
    auto *layout = new QVBoxLayout(column);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(sectionGap());
    // The column takes the width first, up to its maximum; only what is left
    // over goes to the two margins, in equal shares, which is what centres it.
    row->addStretch(1);
    row->addWidget(column, kCardColumnStretch);
    row->addStretch(1);
    pageLayout->addLayout(row);
    return layout;
}

} // namespace speecher::settings
