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
#include <QScrollArea>
#include <QFile>
#include <QTextStream>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStandardItemModel>
#include <QStandardPaths>
#include <QStyle>
#include <QVBoxLayout>

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
    if (!background.isValid()) {
        // Schemes that don't inline a Header group still carry the titlebar
        // color under [WM] — the strip must match the titlebar.
        background = kdeGlobalsColor(QStringLiteral("WM"),
                                     QStringLiteral("activeBackground"));
        if (!foreground.isValid()) {
            foreground = kdeGlobalsColor(QStringLiteral("WM"),
                                         QStringLiteral("activeForeground"));
        }
    }
    result.setColor(QPalette::Window,
                    background.isValid()
                        ? background
                        : base.color(QPalette::Window).darker(110));
    result.setColor(QPalette::WindowText,
                    foreground.isValid() ? foreground : base.color(QPalette::WindowText));
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

void populateAudioInputDevices(QComboBox *combo,
                               const QList<AudioInputDeviceInfo> &devices,
                               const QString &selectedDeviceId)
{
    const QSignalBlocker blocker(combo);
    combo->clear();

    if (devices.isEmpty()) {
        combo->addItem(QStringLiteral("No microphones found"), QString());
        setComboItemEnabled(combo,
                            0,
                            false,
                            QStringLiteral("Connect or enable an input device, then try again."));
        if (!selectedDeviceId.isEmpty()) {
            combo->addItem(QStringLiteral("Missing microphone"), selectedDeviceId);
            setComboItemEnabled(combo,
                                1,
                                false,
                                QStringLiteral("This saved microphone is not currently available."));
            selectData(combo, selectedDeviceId);
        }
        return;
    }

    combo->addItem(QStringLiteral("System default"), QString());
    bool selectedFound = selectedDeviceId.isEmpty();
    for (const AudioInputDeviceInfo &device : devices) {
        combo->addItem(device.isDefault
                           ? QStringLiteral("%1 (default)").arg(device.label)
                           : device.label,
                       device.id);
        selectedFound = selectedFound || device.id == selectedDeviceId;
    }

    if (!selectedFound) {
        combo->addItem(QStringLiteral("Missing microphone"), selectedDeviceId);
        setComboItemEnabled(combo,
                            combo->count() - 1,
                            false,
                            QStringLiteral("This saved microphone is not currently available."));
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

} // namespace speecher::settings
