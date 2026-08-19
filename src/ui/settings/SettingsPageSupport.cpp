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
    form->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
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

} // namespace

QFrame *makeRow(const QString &label,
                const QString &description,
                QWidget *control,
                QWidget *parent,
                QWidget *titleAccessory)
{
    auto *row = new SettingsRow(parent);
    row->setObjectName(QStringLiteral("settingsRow"));
    // A wrapped description is only as tall as the row is narrow, and layouts
    // ignore that unless the policy says the height depends on the width.
    QSizePolicy rowPolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    rowPolicy.setHeightForWidth(true);
    row->setSizePolicy(rowPolicy);

    auto *title = new QLabel(label.endsWith(QLatin1Char(':'))
                                 ? label
                                 : label + QLatin1Char(':'),
                             row);
    title->setObjectName(QStringLiteral("rowTitle"));
    title->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    title->setMinimumWidth(title->fontMetrics().averageCharWidth() * 24);
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
            naturalTextWidth + 8, subtitle->fontMetrics().averageCharWidth() * 62));
        subtitle->setMinimumHeight(subtitle->heightForWidth(subtitle->width()));
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
    return palette.color(QPalette::Link);
#endif
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
    auto *card = new QFrame(parent);
    card->setObjectName(QStringLiteral("settingsCard"));
    // Center the form as a block between stretches. QFormLayout's own
    // formAlignment only centers when every row is a label+field pair; one
    // spanning row (separator, embedded table) makes it lay out full-width
    // and pins every label to the left edge.
    auto *outer = new QHBoxLayout(card);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *host = new QWidget(card);
    host->setObjectName(QStringLiteral("settingsCardForm"));
    host->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
    // Shared content column: every card is at least this wide, so sections on
    // a page share left edges instead of each centering at its own width.
    host->setMinimumWidth(640);
    auto *layout = new QFormLayout(host);
    layout->setContentsMargins(0, 0, 0, 0);
    configureFormLayout(layout);
    outer->addStretch(1);
    outer->addWidget(host);
    outer->addStretch(1);
    return card;
}

QFormLayout *cardFormLayout(QFrame *card)
{
    QWidget *host = card->findChild<QWidget *>(QStringLiteral("settingsCardForm"),
                                               Qt::FindDirectChildrenOnly);
    return host ? qobject_cast<QFormLayout *>(host->layout()) : nullptr;
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

} // namespace speecher::settings
