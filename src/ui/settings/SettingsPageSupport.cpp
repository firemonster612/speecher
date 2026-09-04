#include "ui/settings/SettingsPageSupport.h"

#include <QApplication>
#include <QCheckBox>
#include "dictation/DictationPorts.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QFontMetrics>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QPushButton>
#include <QStyleOption>
#include <QIcon>
#include <QPalette>
#include <QResizeEvent>
#include <QScrollArea>
#include <QFile>
#include <QTextStream>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStandardItemModel>
#include <QStandardPaths>
#include <QStyle>
#include <QVBoxLayout>

#ifdef SPEECHER_WITH_KCOLORSCHEME
#include <KColorScheme>
#endif

namespace speecher::settings {

namespace {

// Kirigami Addons FormCard metrics, expressed through the font like Kirigami
// does: a grid unit is one line of text, small spacing a quarter of it.
int gridUnitFor(const QFont &font)
{
    return QFontMetrics(font).height();
}

} // namespace

int gridUnit() { return gridUnitFor(QApplication::font()); }
int smallSpacing() { return qMax(2, gridUnit() / 4); }
int largeSpacing() { return smallSpacing() * 3; }
int cornerRadius() { return 5; }
int cardMaximumWidth() { return gridUnit() * 30; }

QColor frameColor(const QPalette &palette)
{
    // Kirigami.Theme.frameContrast: a fifth of the text colour over the card.
    const QColor base = palette.color(QPalette::Base);
    const QColor text = palette.color(QPalette::Text);
    return QColor(base.red() + (text.red() - base.red()) / 5,
                  base.green() + (text.green() - base.green()) / 5,
                  base.blue() + (text.blue() - base.blue()) / 5);
}

// The FormCard container. Kirigami draws it as a rounded rectangle in the
// view colour with a thin frame; there is no Qt Widgets equivalent, so this
// is the one place a shape is painted, with palette colours only.
class FormCardFrame final : public QFrame {
public:
    explicit FormCardFrame(QWidget *parent)
        : QFrame(parent)
    {
        setAttribute(Qt::WA_TranslucentBackground, false);
        setAutoFillBackground(false);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const qreal radius = cornerRadius();
        QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        painter.setPen(QPen(frameColor(palette()), 1));
        painter.setBrush(palette().color(QPalette::Base));
        painter.drawRoundedRect(box, radius, radius);
    }
};

// FormButtonDelegate: the whole row is the button, text on the left, an arrow
// on the right, and the style's own item hover as feedback.
class FormButtonRow final : public QPushButton {
public:
    explicit FormButtonRow(QWidget *parent)
        : QPushButton(parent)
    {
        setCursor(Qt::PointingHandCursor);
        setFlat(true);
        setFocusPolicy(Qt::StrongFocus);
        // The row is as tall as its text, not as tall as a button caption.
        QSizePolicy policy(QSizePolicy::Expanding, QSizePolicy::Minimum);
        policy.setHeightForWidth(true);
        setSizePolicy(policy);
    }

    QSize sizeHint() const override
    {
        return layout() ? layout()->sizeHint() : QPushButton::sizeHint();
    }

    QSize minimumSizeHint() const override
    {
        return layout() ? layout()->minimumSize() : QPushButton::minimumSizeHint();
    }

    bool hasHeightForWidth() const override { return layout() && layout()->hasHeightForWidth(); }

    int heightForWidth(int width) const override
    {
        return layout() ? layout()->heightForWidth(width) : QPushButton::heightForWidth(width);
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QPushButton::resizeEvent(event);
        if (QLayout *rowLayout = layout()) {
            setMinimumHeight(rowLayout->hasHeightForWidth() ? rowLayout->heightForWidth(width())
                                                            : rowLayout->minimumSize().height());
        }
    }


    void paintEvent(QPaintEvent *) override
    {
        if (!underMouse() && !isDown() && !hasFocus()) {
            return;
        }
        QStyleOptionViewItem option;
        option.initFrom(this);
        option.rect = rect();
        option.viewItemPosition = QStyleOptionViewItem::OnlyOne;
        option.state |= QStyle::State_MouseOver;
        if (isDown()) {
            option.state |= QStyle::State_Selected;
        }
        option.showDecorationSelected = true;
        QPainter painter(this);
        style()->drawPrimitive(QStyle::PE_PanelItemViewItem, &option, &painter, this);
    }
};


QColor separatorColor(const QPalette &palette)
{
    const QColor window = palette.color(QPalette::Window);
    const QColor text = palette.color(QPalette::WindowText);
    return QColor((window.red() * 3 + text.red()) / 4,
                  (window.green() * 3 + text.green()) / 4,
                  (window.blue() * 3 + text.blue()) / 4);
}

QFrame *makeSeparator(QWidget *parent)
{
    auto *line = new QFrame(parent);
    line->setObjectName(QStringLiteral("settingsSeparator"));
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Plain);
    line->setFixedHeight(1);
    // Sunken frames vanish on dark schemes; blend a quarter of the text
    // color into the window color, the way Kirigami derives separators.
    QPalette blended(parent->palette());
    blended.setColor(QPalette::WindowText, separatorColor(parent->palette()));
    line->setPalette(blended);
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
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    form->setLabelAlignment(Qt::AlignRight);
}

namespace {

// QFormLayout sizes a row from its size hint, which for a word-wrapped
// description is measured at the hint width rather than the real one. The row
// therefore has to claim the height its own layout needs once it knows how
// wide it is, or a description that wraps gets clipped.
class SettingsRow final : public QFrame {
public:
    explicit SettingsRow(QWidget *parent)
        : QFrame(parent)
    {
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QFrame::resizeEvent(event);
        if (QLayout *rowLayout = layout()) {
            setMinimumHeight(rowLayout->hasHeightForWidth()
                                 ? rowLayout->heightForWidth(width())
                                 : rowLayout->minimumSize().height());
        }
    }
};

// The wrapping text beside a check box whose sentence is too long to be the
// box's own text. Clicking the words toggles the box, as a check box label does.
class CheckBoxCaption final : public QLabel {
public:
    CheckBoxCaption(const QString &text, QCheckBox *checkBox, QWidget *parent)
        : QLabel(text, parent)
        , m_checkBox(checkBox)
    {
        setObjectName(QStringLiteral("checkBoxCaption"));
        setWordWrap(true);
        setBuddy(checkBox);
    }

protected:
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        QLabel::mouseReleaseEvent(event);
        if (event->button() == Qt::LeftButton && rect().contains(event->pos())
            && m_checkBox->isEnabled()) {
            m_checkBox->toggle();
        }
    }

private:
    QCheckBox *m_checkBox;
};

} // namespace

QFrame *makeRow(const QString &label,
                const QString &description,
                QWidget *control,
                QWidget *parent,
                QWidget *titleAccessory,
                bool dynamicDescription)
{
    // One card row: title and an optional one-line subtitle on the left, the
    // control on the right, vertically centred. A text field is the exception:
    // it wants the full row width, so it sits below the title.
    auto *row = new SettingsRow(parent);
    row->setObjectName(QStringLiteral("settingsRow"));
    QSizePolicy rowPolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    rowPolicy.setHeightForWidth(true);
    row->setSizePolicy(rowPolicy);

    auto *checkBox = qobject_cast<QCheckBox *>(control);
    // A check box row reads as one sentence: the sentence is the title and
    // clicking the words toggles the box, as a check box label would.
    const QString titleText = checkBox && !description.isEmpty() ? description : label;
    const QString subtitleText = checkBox ? QString() : description;

    auto *text = new QWidget(row);
    text->setObjectName(QStringLiteral("rowLabelCell"));
    text->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto *textLayout = new QVBoxLayout(text);
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(0);

    QLabel *title = checkBox ? new CheckBoxCaption(titleText, checkBox, text)
                             : new QLabel(titleText, text);
    title->setObjectName(QStringLiteral("rowTitle"));
    title->setWordWrap(true);
    title->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    if (checkBox) {
        checkBox->setText(QString());
        checkBox->setAccessibleName(titleText);
    }
    if (titleAccessory) {
        auto *titleRow = new QWidget(text);
        titleRow->setObjectName(QStringLiteral("rowText"));
        auto *titleLayout = new QHBoxLayout(titleRow);
        titleLayout->setContentsMargins(0, 0, 0, 0);
        titleLayout->setSpacing(tightSpacing());
        titleLayout->addWidget(title, 0, Qt::AlignVCenter);
        titleLayout->addWidget(titleAccessory, 0, Qt::AlignVCenter);
        titleLayout->addStretch(1);
        textLayout->addWidget(titleRow);
    } else {
        textLayout->addWidget(title);
    }
    if (!subtitleText.isEmpty() || dynamicDescription) {
        // Kirigami's "grayed out description": the small font in the
        // placeholder colour. Kept (hidden) when the row fills it in later.
        auto *subtitle = new QLabel(subtitleText, text);
        subtitle->setObjectName(QStringLiteral("rowDescription"));
        subtitle->setWordWrap(true);
        subtitle->setForegroundRole(QPalette::PlaceholderText);
        subtitle->setFont(smallFont(subtitle->font()));
        subtitle->setVisible(!subtitleText.isEmpty());
        textLayout->addWidget(subtitle);
    }

    const bool fullWidthControl =
        control->sizePolicy().horizontalPolicy() == QSizePolicy::Expanding && !checkBox;
    const QMargins padding = rowPadding();
    if (fullWidthControl) {
        auto *layout = new QVBoxLayout(row);
        layout->setContentsMargins(padding);
        layout->setSpacing(smallSpacing());
        layout->addWidget(text);
        layout->addWidget(control);
        return row;
    }
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(padding);
    layout->setSpacing(largeSpacing());
    layout->addWidget(text, 1, Qt::AlignVCenter);
    layout->addWidget(control, 0, Qt::AlignRight | Qt::AlignVCenter);
    return row;
}

void addCardRow(QFormLayout *layout, QWidget *row, QWidget *parent)
{
    // Rows in a card are separated by the same hairline the rest of the window
    // uses; the card's frame bounds the first and last.
    if (layout->rowCount() > 0) {
        auto *inset = new QWidget(parent);
        inset->setObjectName(QStringLiteral("rowSeparator"));
        auto *insetLayout = new QHBoxLayout(inset);
        insetLayout->setContentsMargins(gridUnit(), 0, gridUnit(), 0);
        insetLayout->addWidget(makeSeparator(inset));
        layout->addRow(inset);
    }
    layout->addRow(row);
}

void addRow(QFormLayout *layout, QFrame *row, QWidget *parent, bool addSeparator)
{
    Q_UNUSED(addSeparator);
    addCardRow(layout, row, parent);
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

// kdeglobals is a KConfig file, not valid QSettings INI: subgroup section
// lines like "[Colors:Header][Inactive]" derail QSettings' parser and its
// group lookup. Scan for the exact section header line instead.
static QColor kdeGlobalsColor(const QString &section, const QString &key)
{
    QFile file(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
               + QStringLiteral("/kdeglobals"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    const QString wantedHeader = QLatin1Char('[') + section + QLatin1Char(']');
    bool inSection = false;
    QTextStream stream(&file);
    while (!stream.atEnd()) {
        const QString line = stream.readLine().trimmed();
        if (line.startsWith(QLatin1Char('['))) {
            inSection = line == wantedHeader;
            continue;
        }
        if (!inSection || !line.startsWith(key + QLatin1Char('='))) {
            continue;
        }
        const QStringList channels =
            line.mid(key.size() + 1).split(QLatin1Char(','));
        if (channels.size() != 3) {
            return {};
        }
        int rgb[3];
        for (int index = 0; index < 3; ++index) {
            bool valid = false;
            rgb[index] = channels.at(index).trimmed().toInt(&valid);
            if (!valid || rgb[index] < 0 || rgb[index] > 255) {
                return {};
            }
        }
        return QColor(rgb[0], rgb[1], rgb[2]);
    }
    return {};
}

bool kdePlatformThemeActive()
{
    // plasma-integration publishes the scheme it loaded on the application;
    // no other platform theme sets this property.
    if (qApp && qApp->property("KDE_COLOR_SCHEME_PATH").isValid()) {
        return true;
    }
    return qEnvironmentVariable("QT_QPA_PLATFORMTHEME")
               .compare(QStringLiteral("kde"), Qt::CaseInsensitive)
        == 0;
}

QPalette headerPalette(const QPalette &base)
{
    if (kdePlatformThemeActive()) {
        return kdeHeaderPalette(base);
    }
    QPalette result(base);
    const QColor shade = base.color(QPalette::Active, QPalette::Window).darker(110);
    result.setColor(QPalette::Active, QPalette::Window, shade);
    result.setColor(QPalette::Inactive, QPalette::Window, shade);
    result.setColor(QPalette::Disabled, QPalette::Window, shade);
    return result;
}

QPalette kdeHeaderPalette(const QPalette &base)
{
    QPalette result(base);
    QColor background = kdeGlobalsColor(QStringLiteral("Colors:Header"),
                                        QStringLiteral("BackgroundNormal"));
    QColor foreground = kdeGlobalsColor(QStringLiteral("Colors:Header"),
                                        QStringLiteral("ForegroundNormal"));
    QColor inactiveBackground = kdeGlobalsColor(
        QStringLiteral("Colors:Header][Inactive"), QStringLiteral("BackgroundNormal"));
    QColor inactiveForeground = kdeGlobalsColor(
        QStringLiteral("Colors:Header][Inactive"), QStringLiteral("ForegroundNormal"));
    if (!background.isValid()) {
        // Schemes that don't inline a Header group still carry the titlebar
        // color under [WM] — the strip must match the titlebar.
        background = kdeGlobalsColor(QStringLiteral("WM"),
                                     QStringLiteral("activeBackground"));
        foreground = kdeGlobalsColor(QStringLiteral("WM"),
                                     QStringLiteral("activeForeground"));
        inactiveBackground = kdeGlobalsColor(QStringLiteral("WM"),
                                             QStringLiteral("inactiveBackground"));
        inactiveForeground = kdeGlobalsColor(QStringLiteral("WM"),
                                             QStringLiteral("inactiveForeground"));
    }
    result.setColor(QPalette::Active, QPalette::Window,
                    background.isValid() ? background
                                         : base.color(QPalette::Active, QPalette::Window).darker(110));
    result.setColor(QPalette::Active, QPalette::WindowText,
                    foreground.isValid() ? foreground
                                         : base.color(QPalette::Active, QPalette::WindowText));
    const QColor resolvedInactiveBackground =
        inactiveBackground.isValid() ? inactiveBackground : result.color(QPalette::Active, QPalette::Window);
    const QColor resolvedInactiveForeground =
        inactiveForeground.isValid() ? inactiveForeground : result.color(QPalette::Active, QPalette::WindowText);
    result.setColor(QPalette::Inactive, QPalette::Window, resolvedInactiveBackground);
    result.setColor(QPalette::Inactive, QPalette::WindowText, resolvedInactiveForeground);
    result.setColor(QPalette::Disabled, QPalette::Window, resolvedInactiveBackground);
    result.setColor(QPalette::Disabled, QPalette::WindowText, resolvedInactiveForeground);
    return result;
}

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

QList<RowOption> audioInputDeviceOptions(const QList<AudioInputDeviceInfo> &devices)
{
    QList<RowOption> options;
    options.reserve(devices.size());
    for (const AudioInputDeviceInfo &device : devices) {
        options.append({device.id,
                        device.isDefault ? QStringLiteral("%1 (default)").arg(device.label)
                                         : device.label});
    }
    return options;
}

void populateAudioInputDevices(QComboBox *combo,
                               const QList<AudioInputDeviceInfo> &devices,
                               const QString &selectedDeviceId)
{
    const QSignalBlocker blocker(combo);
    combo->clear();
    for (const RowOption &option : audioDeviceOptions(audioInputDeviceOptions(devices),
                                                      selectedDeviceId)) {
        combo->addItem(option.label, option.id);
        if (!option.enabled) {
            setComboItemEnabled(combo, combo->count() - 1, false, option.help);
        }
    }
    selectData(combo, selectedDeviceId);
}

QColor positiveTextColor(const QPalette &palette)
{
#ifdef SPEECHER_WITH_KCOLORSCHEME
    const KColorScheme colors(palette.currentColorGroup(), KColorScheme::View);
    return colors.foreground(KColorScheme::PositiveText).color();
#else
    // Without a scheme that names a positive colour, plain text: link blue
    // would read as something to click.
    return palette.color(QPalette::WindowText);
#endif
}

QLabel *makeSectionLabel(const QString &text, QWidget *parent)
{
    // FormHeader: bold text right above its card, inset like the rows.
    auto *section = new QLabel(text, parent);
    section->setObjectName(QStringLiteral("sectionLabel"));
    section->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    section->setContentsMargins(gridUnit(), 0, gridUnit(), smallSpacing());
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

void addSectionRow(QFormLayout *form, const QString &title, QWidget *parent)
{
    // Section titles live inside the card's centered form block so the title
    // and its content share a left edge instead of the title hugging the page
    // margin while the form floats in the middle.
    if (form->rowCount() > 0) {
        auto *gap = new QWidget(parent);
        gap->setFixedHeight(groupGap() / 2);
        form->addRow(gap);
    }
    form->addRow(makeSectionLabel(title, parent));
}

QFrame *makeSettingsCard(QWidget *parent)
{
    auto *card = new FormCardFrame(parent);
    card->setObjectName(QStringLiteral("settingsCard"));
    card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto *outer = new QVBoxLayout(card);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *host = new QWidget(card);
    host->setObjectName(QStringLiteral("settingsCardForm"));
    host->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto *layout = new QFormLayout(host);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setVerticalSpacing(0);
    configureFormLayout(layout);
    outer->addWidget(host);
    return card;
}

QPushButton *makeButtonRow(const QString &title, const QString &description, QWidget *parent)
{
    auto *row = new FormButtonRow(parent);
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(rowPadding());
    layout->setSpacing(largeSpacing());
    auto *text = new QWidget(row);
    text->setObjectName(QStringLiteral("rowLabelCell"));
    text->setAttribute(Qt::WA_TransparentForMouseEvents);
    auto *textLayout = new QVBoxLayout(text);
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(0);
    auto *titleLabel = new QLabel(title, text);
    titleLabel->setObjectName(QStringLiteral("rowTitle"));
    titleLabel->setWordWrap(true);
    textLayout->addWidget(titleLabel);
    if (!description.isEmpty()) {
        auto *subtitle = new QLabel(description, text);
        subtitle->setObjectName(QStringLiteral("rowDescription"));
        subtitle->setWordWrap(true);
        subtitle->setForegroundRole(QPalette::PlaceholderText);
        subtitle->setFont(smallFont(subtitle->font()));
        textLayout->addWidget(subtitle);
    }
    auto *arrow = new QLabel(row);
    arrow->setObjectName(QStringLiteral("rowArrow"));
    arrow->setAttribute(Qt::WA_TransparentForMouseEvents);
    const QIcon icon = QIcon::fromTheme(QStringLiteral("arrow-right"),
                                        QIcon::fromTheme(QStringLiteral("go-next")));
    const int extent = row->style()->pixelMetric(QStyle::PM_SmallIconSize, nullptr, row);
    arrow->setPixmap(icon.pixmap(extent, extent));
    layout->addWidget(text, 1, Qt::AlignVCenter);
    layout->addWidget(arrow, 0, Qt::AlignVCenter);
    row->setAccessibleName(title);
    return row;
}

QMargins rowPadding()
{
    // AbstractFormDelegate: a grid unit sideways, large plus small vertically.
    return QMargins(gridUnit(), largeSpacing() + smallSpacing(), gridUnit(), largeSpacing() + smallSpacing());
}

QFont smallFont(const QFont &font)
{
    // Kirigami.Theme.smallFont: one step below the default.
    QFont small = font;
    if (small.pointSizeF() > 0) {
        small.setPointSizeF(small.pointSizeF() * 0.9);
    } else if (small.pixelSize() > 0) {
        small.setPixelSize(qMax(1, small.pixelSize() * 9 / 10));
    }
    return small;
}

QFormLayout *cardFormLayout(QWidget *card)
{
    QWidget *host = card->findChild<QWidget *>(QStringLiteral("settingsCardForm"));
    return host ? qobject_cast<QFormLayout *>(host->layout()) : nullptr;
}

QWidget *centerColumn(QWidget *content, QWidget *parent)
{
    // The page itself is capped and centred (see makeSettingsPage), so every
    // section simply fills the page width and all cards share the same edges.
    content->setParent(parent);
    content->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    return content;
}

void configurePageScroll(QScrollArea *scroll, QWidget *content)
{
    // The one page container: a frameless scroll area on the window colour
    // whose content is capped at the card width plus its own margins and
    // centred when the pane is wider. Every page, composed or schema-driven,
    // goes through here so their cards share the same edges.
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setBackgroundRole(QPalette::Window);
    scroll->viewport()->setBackgroundRole(QPalette::Window);
    scroll->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    content->setAutoFillBackground(false);
    const QMargins margins = content->layout() ? content->layout()->contentsMargins() : QMargins();
    content->setMaximumWidth(cardMaximumWidth() + margins.left() + margins.right());
    scroll->setWidget(content);
}

QVBoxLayout *makeSettingsPage(QScrollArea *scroll)
{
    scroll->setObjectName(QStringLiteral("settingsScroll"));
    auto *page = new QWidget(scroll);
    auto *layout = new QVBoxLayout(page);
    applyPageMargins(layout);
    configurePageScroll(scroll, page);
    return layout;
}

} // namespace speecher::settings
